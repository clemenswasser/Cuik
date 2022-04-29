////////////////////////////////
// EXPRESSIONS
//
// Quick reference:
// https://en.cppreference.com/w/c/language/operator_precedence
////////////////////////////////
// This file is included into parser.h, it's parser of the parser module and is 
// completely static
static ExprIndex parse_expr_l14(TranslationUnit* tu, TokenStream* restrict s);
static ExprIndex parse_expr_l2(TranslationUnit* tu, TokenStream* restrict s);
static ExprIndex parse_expr(TranslationUnit* tu, TokenStream* restrict s);

static ExprIndex parse_function_literal(TranslationUnit* tu, TokenStream* restrict s, TypeIndex type) {
	SourceLocIndex loc = tokens_get(s)->location;
	
	// if the literal doesn't have a parameter list it will inherit from the `type`
	if (tokens_get(s)->type == '(') {
		tokens_next(s);
		
		// this might break on typeof because it's fucking weird...
		// TODO(NeGate): error messages... no attributes here
		Attribs attr = { 0 };
		type = parse_declspec(tu, s, &attr);
		type = parse_declarator(tu, s, type, true, true).type;
		
		expect(s, ')');
	}
	
	if (tu->types[type].kind == KIND_PTR) {
		type = tu->types[type].ptr_to;
	}
	
	if (tu->types[type].kind != KIND_FUNC) {
		generic_error(s, "Function literal base type is not a function type");
	}
	
	// Because of the "not a lambda" nature of the function literals they count as
	// "scoped top level statements" which doesn't particularly change anything for
	// it but is interesting to think about internally
	Stmt* n = make_stmt(tu, s, STMT_FUNC_DECL);
	n->loc = loc;
	n->decl = (struct StmtDecl){
		.type = type,
		.name = NULL,
		.attrs = {
			.is_root = true, // to avoid it being vaporized
			.is_inline = true
		},
		.initial_as_stmt = NULL
	};
	arrput(tu->top_level_stmts, n);
	
	// Don't wanna share locals between literals and their parents, by the current
	// design of APIs with C callbacks this is aight because we already manually pass
	// user context, we just improve "locality" of these functions to their reference
	int old_start = local_symbol_start;
	local_symbol_start = local_symbol_count;
	{
		parse_function_definition(tu, s, n);
	}
	local_symbol_start = old_start;
	
	ExprIndex e = make_expr(tu);
	tu->exprs[e] = (Expr) {
		.op = EXPR_FUNCTION,
		.loc = loc,
		.type = type,
		.func = { n }
	};
	return e;
}

// NOTE(NeGate): This function will push all nodes it makes onto the temporary
// storage where parse_initializer will move them into permanent storage.
static void parse_initializer_member(TranslationUnit* tu, TokenStream* restrict s) {
	// Parse designator, it's just chains of:
	// [const-expr]
	// .identifier
	InitNode* current = NULL;
	try_again: {
		if (tokens_get(s)->type == '[') {
			tokens_next(s);
			
			int start = parse_const_expr(tu, s);
			if (start < 0) {
				// TODO(NeGate): Error messages
				generic_error(s, "Array initializer range is broken.");
			}
			
			// GNU-extension: array range initializer
			int count = 1;
			if (tokens_get(s)->type == TOKEN_TRIPLE_DOT) {
				tokens_next(s);
				
				count = parse_const_expr(tu, s) - start;
				if (count <= 1) {
					// TODO(NeGate): Error messages
					generic_error(s, "Array initializer range is broken.");
				}
			}
			expect(s, ']');
			
			current = (InitNode*) tls_push(sizeof(InitNode));
			*current = (InitNode) {
				.mode = INIT_ARRAY,
				.kids_count = 1,
				.start = start,
				.count = count
			};
			goto try_again;
		}
		
		if (tokens_get(s)->type == '.') {
			tokens_next(s);
			
			Token* t = tokens_get(s);
			Atom name = atoms_put(t->end - t->start, t->start);
			tokens_next(s);
			
			current = (InitNode*) tls_push(sizeof(InitNode));
			*current = (InitNode) {
				.mode = INIT_MEMBER,
				.kids_count = 1,
				.member_name = name
			};
			goto try_again;
		}
	}
	
	// if it has no designator make a dummy one.
	if (!current) {
		current = (InitNode*) tls_push(sizeof(InitNode));
		*current = (InitNode) {
			.kids_count = 1,
			.member_name = NULL
		};
	} else {
		expect(s, '=');
	}
	
	// it can either be a normal expression
	// or a nested designated initializer
	if (tokens_get(s)->type == '{') {
		tokens_next(s);
		
		size_t local_count = 0;
		
		// don't expect one the first time
		bool expect_comma = false;
		while (tokens_get(s)->type != '}') {
			if (expect_comma) {
				expect(s, ',');
			} else expect_comma = true;
			
			parse_initializer_member(tu, s);
			local_count += 1;
		}
		
		current->kids_count = local_count;
		expect(s, '}');
	} else {
		// parse without comma operator
		current->kids_count = 0;
		current->expr = parse_expr_l14(tu, s);
	}
}

static ExprIndex parse_initializer(TranslationUnit* tu, TokenStream* restrict s, TypeIndex type) {
	size_t count = 0;
	InitNode* start = tls_save();
	
	// don't expect one the first time
	bool expect_comma = false;
	while (tokens_get(s)->type != '}') {
		if (expect_comma) {
			expect(s, ',');
			
			if (tokens_get(s)->type == '}') break;
		} else expect_comma = true;
		
		parse_initializer_member(tu, s);
		count += 1;
	}
	
	size_t total_node_count = ((InitNode*)tls_save()) - start;
	expect(s, '}');
	
	InitNode* permanent_store = arena_alloc(&thread_arena, total_node_count * sizeof(InitNode), _Alignof(InitNode));
	memcpy(permanent_store, start, total_node_count * sizeof(InitNode));
	tls_restore(start);
	
	ExprIndex e = make_expr(tu);
	tu->exprs[e] = (Expr) {
		.op = EXPR_INITIALIZER,
		.loc = tokens_get(s)->location,
		.init = { type, count, permanent_store }
	};
	return e;
}

static ExprIndex parse_expr_l0(TranslationUnit* tu, TokenStream* restrict s) {
	Token* t = tokens_get(s);
	SourceLocIndex loc = t->location;
	
	if (t->type == '(') {
		tokens_next(s);
		ExprIndex e = parse_expr(tu, s);
		expect(s, ')');
		
		return e;
	} else if (t->type == TOKEN_IDENTIFIER) {
		Symbol* sym = find_local_symbol(s);
		
		ExprIndex e = make_expr(tu);
		if (sym) {
			if (sym->storage_class == STORAGE_PARAM) {
				tu->exprs[e] = (Expr) {
					.op = EXPR_PARAM,
					.loc = loc,
					.param_num = sym->param_num
				};
			} else {
				tu->exprs[e] = (Expr) {
					.op = EXPR_SYMBOL,
					.loc = loc,
					.symbol = sym->stmt
				};
			}
		} else {
			// We'll defer any global identifier resolution
			Token* t = tokens_get(s);
			Atom name = atoms_put(t->end - t->start, t->start);
			
			ptrdiff_t search = shgeti(labels, name);
			if (search >= 0) {
				tu->exprs[e] = (Expr) {
					.op = EXPR_SYMBOL,
					.loc = loc,
					.symbol = labels[search].value
				};
			} else {
#if OUT_OF_ORDER_CRAP
				Symbol* search = find_global_symbol((const char*) name);
				if (search != NULL) {
					tu->exprs[e] = (Expr) {
						.op = EXPR_SYMBOL,
						.loc = loc,
						.symbol = search->stmt
					};
				} else {
					report(REPORT_ERROR, &s->line_arena[loc], "could not resolve symbol: %s", name);
					
					tu->exprs[e] = (Expr) {
						.op = EXPR_UNKNOWN_SYMBOL,
						.loc = loc,
						.unknown_sym = name
					};
				}
#else
				tu->exprs[e] = (Expr) {
					.op = EXPR_UNKNOWN_SYMBOL,
					.loc = loc,
					.unknown_sym = name
				};
#endif
			}
		}
		
		tokens_next(s);
		return e;
	} else if (tokens_get(s)->type == '@') {
		// function literals are a Cuik extension
		// TODO(NeGate): error messages
		if (settings.pedantic) {
			generic_error(s, "Function literals are a cuik-extension");
		}
		
		tokens_next(s);
		return parse_function_literal(tu, s, 0);
	} else if (tokens_get(s)->type == TOKEN_FLOAT) {
		Token* t = tokens_get(s);
		bool is_float32 = t->end[-1] == 'f';
		double i = parse_float(t->end - t->start, (const char*)t->start);
		
		ExprIndex e = make_expr(tu);
		tu->exprs[e] = (Expr) {
			.op = is_float32 ? EXPR_FLOAT32 : EXPR_FLOAT64,
			.loc = loc,
			.float_num = i
		};
		
		tokens_next(s);
		return e;
	} else if (tokens_get(s)->type == TOKEN_INTEGER) {
		Token* t = tokens_get(s);
		IntSuffix suffix;
		uint64_t i = parse_int(t->end - t->start, (const char*)t->start, &suffix);
		
		ExprIndex e = make_expr(tu);
		tu->exprs[e] = (Expr) {
			.op = EXPR_INT,
			.loc = loc,
			.int_num = { i, suffix }
		};
		
		tokens_next(s);
		return e;
	} else if (tokens_get(s)->type == TOKEN_STRING_SINGLE_QUOTE) {
		Token* t = tokens_get(s);
		
		ExprIndex e = make_expr(tu);
		tu->exprs[e] = (Expr) {
			.op = EXPR_CHAR,
			.loc = loc,
			.str.start = t->start,
			.str.end = t->end
		};
		
		tokens_next(s);
		return e;
	} else if (tokens_get(s)->type == TOKEN_STRING_DOUBLE_QUOTE ||
			   tokens_get(s)->type == TOKEN_STRING_WIDE_DOUBLE_QUOTE) {
		Token* t = tokens_get(s);
		bool is_wide = (tokens_get(s)->type == TOKEN_STRING_WIDE_DOUBLE_QUOTE);
		
		ExprIndex e = make_expr(tu);
		tu->exprs[e] = (Expr) {
			.op = is_wide ? EXPR_WSTR : EXPR_STR,
			.loc = loc,
			.str.start = t->start,
			.str.end = t->end
		};
		
		size_t saved_lexer_pos = s->current;
		tokens_next(s);
		
		if (tokens_get(s)->type == TOKEN_STRING_DOUBLE_QUOTE ||
			tokens_get(s)->type == TOKEN_STRING_WIDE_DOUBLE_QUOTE) {
			// Precompute length
			s->current = saved_lexer_pos;
			size_t total_len = (t->end - t->start);
			while (tokens_get(s)->type == TOKEN_STRING_DOUBLE_QUOTE ||
				   tokens_get(s)->type == TOKEN_STRING_WIDE_DOUBLE_QUOTE) {
				Token* segment = tokens_get(s);
				total_len += (segment->end - segment->start) - 2;
				tokens_next(s);
			}
			
			size_t curr = 0;
			char* buffer = arena_alloc(&thread_arena, total_len + 3, 4);
			
			buffer[curr++] = '\"';
			
			// Fill up the buffer
			s->current = saved_lexer_pos;
			while (tokens_get(s)->type == TOKEN_STRING_DOUBLE_QUOTE ||
				   tokens_get(s)->type == TOKEN_STRING_WIDE_DOUBLE_QUOTE) {
				Token* segment = tokens_get(s);
				
				size_t len = segment->end - segment->start;
				memcpy(&buffer[curr], segment->start + 1, len - 2);
				curr += len - 2;
				
				tokens_next(s);
			}
			
			buffer[curr++] = '\"';
			
			tu->exprs[e].str.start = (const unsigned char*)buffer;
			tu->exprs[e].str.end = (const unsigned char*)(buffer + curr);
		}
		
		return e;
	} else if (t->type == TOKEN_KW_Generic) {
		tokens_next(s);
		
		SourceLoc* opening_loc = &s->line_arena[tokens_get(s)->location];
		expect(s, '(');
		
		// controlling expression followed by a comma
		ExprIndex e = make_expr(tu);
		ExprIndex controlling_expr = parse_expr_l14(tu, s);
		
		tu->exprs[e] = (Expr) {
			.op = EXPR_GENERIC,
			.loc = loc,
			.generic_ = {
				.controlling_expr = controlling_expr
			}
		};
		expect(s, ',');
		
		size_t entry_count = 0;
		C11GenericEntry* entries = tls_save();
		
		SourceLoc* default_loc = NULL;
		while (tokens_get(s)->type != ')') {
			if (tokens_get(s)->type == TOKEN_KW_default) {
				if (default_loc) {
					report_two_spots(REPORT_ERROR, default_loc, 
									 &s->line_arena[tokens_get(s)->location], 
									 "multiple default cases on _Generic",
									 NULL, NULL, NULL);
					
					// maybe do some error recovery
					abort();
				}
				
				default_loc = &s->line_arena[tokens_get(s)->location];
				expect(s, ':');
				ExprIndex expr = parse_expr_l14(tu, s);
				
				// the default case is like a normal entry but without a type :p
				tls_push(sizeof(C11GenericEntry));
				entries[entry_count++] = (C11GenericEntry){
					.key   = TYPE_NONE,
					.value = expr
				};
			} else {
				TypeIndex type = parse_typename(tu, s);
				assert(type != 0 && "TODO: error recovery");
				
				expect(s, ':');
				ExprIndex expr = parse_expr_l14(tu, s);
				
				tls_push(sizeof(C11GenericEntry));
				entries[entry_count++] = (C11GenericEntry){
					.key   = type,
					.value = expr
				};
			}
			
			// exit if it's not a comma
			if (tokens_get(s)->type != ',') break;
			tokens_next(s);
		}
		
		expect_closing_paren(s, opening_loc);
		
		// move it to a more permanent storage
		C11GenericEntry* dst = arena_alloc(&thread_arena, entry_count * sizeof(C11GenericEntry), _Alignof(C11GenericEntry));
		memcpy(dst, entries, entry_count * sizeof(C11GenericEntry));
		
		tu->exprs[e].generic_.case_count = entry_count;
		tu->exprs[e].generic_.cases = dst;
		
		tls_restore(entries);
		return e;
	} else {
		generic_error(s, "Could not parse expression!");
	}
}

static ExprIndex parse_expr_l1(TranslationUnit* tu, TokenStream* restrict s) {
	ExprIndex e = 0;
	if (tokens_get(s)->type == '(') {
		tokens_next(s);
		
		if (is_typename(s)) {
			TypeIndex type = parse_typename(tu, s);
			expect(s, ')');
			
			if (tokens_get(s)->type == '{') {
				tokens_next(s);
				
				e = parse_initializer(tu, s, type);
			} else {
				ExprIndex base = parse_expr_l2(tu, s);
				e = make_expr(tu);
				
				tu->exprs[e] = (Expr) {
					.op = EXPR_CAST,
					.loc = tokens_get(s)->location,
					.cast = { type, base }
				};
			}
		}
		
		if (!e) tokens_prev(s);
	}
	
	SourceLocIndex loc = tokens_get(s)->location;
	if (!e) e = parse_expr_l0(tu, s);
	
	// after any of the: [] () . ->
	// it'll restart and take a shot at matching another
	// piece of the expression.
	try_again: {
		if (tokens_get(s)->type == '[') {
			ExprIndex base = e;
			e = make_expr(tu);
			
			tokens_next(s);
			ExprIndex index = parse_expr(tu, s);
			expect(s, ']');
			
			tu->exprs[e] = (Expr) {
				.op = EXPR_SUBSCRIPT,
				.loc = loc,
				.subscript = { base, index }
			};
			goto try_again;
		}
		
		// Pointer member access
		if (tokens_get(s)->type == TOKEN_ARROW) {
			tokens_next(s);
			if (tokens_get(s)->type != TOKEN_IDENTIFIER) {
				generic_error(s, "Expected identifier after member access a.b");
			}
			
			Token* t = tokens_get(s);
			Atom name = atoms_put(t->end - t->start, t->start);
			
			ExprIndex base = e;
			e = make_expr(tu);
			tu->exprs[e] = (Expr) {
				.op = EXPR_ARROW,
				.loc = loc,
				.dot_arrow = { .base = base, .name = name }
			};
			
			tokens_next(s);
			goto try_again;
		}
		
		// Member access
		if (tokens_get(s)->type == '.') {
			tokens_next(s);
			if (tokens_get(s)->type != TOKEN_IDENTIFIER) {
				generic_error(s, "Expected identifier after member access a.b");
			}
			
			Token* t = tokens_get(s);
			Atom name = atoms_put(t->end - t->start, t->start);
			
			ExprIndex base = e;
			e = make_expr(tu);
			tu->exprs[e] = (Expr) {
				.op = EXPR_DOT,
				.loc = loc,
				.dot_arrow = { .base = base, .name = name }
			};
			
			tokens_next(s);
			goto try_again;
		}
		
		// Function call
		if (tokens_get(s)->type == '(') {
			tokens_next(s);
			
			ExprIndex target = e;
			e = make_expr(tu);
			
			size_t param_count = 0;
			void* params = tls_save();
			
			while (tokens_get(s)->type != ')') {
				if (param_count) {
					expect(s, ',');
				}
				
				// NOTE(NeGate): This is a funny little work around because
				// i don't wanna parse the comma operator within the expression
				// i wanna parse it here so we just skip it.
				ExprIndex e = parse_expr_l14(tu, s);
				*((ExprIndex*) tls_push(sizeof(ExprIndex))) = e;
				param_count++;
			}
			
			if (tokens_get(s)->type != ')') {
				generic_error(s, "Unclosed parameter list!");
			}
			tokens_next(s);
			
			// Copy parameter refs into more permanent storage
			ExprIndex* param_start = arena_alloc(&thread_arena, param_count * sizeof(ExprIndex), _Alignof(ExprIndex));
			memcpy(param_start, params, param_count * sizeof(ExprIndex));
			
			tu->exprs[e] = (Expr) {
				.op = EXPR_CALL,
				.loc = loc,
				.call = { target, param_count, param_start }
			};
			
			tls_restore(params);
			goto try_again;
		}
		
		// post fix, you can only put one and just after all the other operators
		// in this precendence.
		if (tokens_get(s)->type == TOKEN_INCREMENT || tokens_get(s)->type == TOKEN_DECREMENT) {
			bool is_inc = tokens_get(s)->type == TOKEN_INCREMENT;
			tokens_next(s);
			
			ExprIndex src = e;
			
			e = make_expr(tu);
			tu->exprs[e] = (Expr) {
				.op = is_inc ? EXPR_POST_INC : EXPR_POST_DEC,
				.loc = loc,
				.unary_op.src = src
			};
		}
		
		return e;
	}
}

// deref* address& negate- sizeof _Alignof cast
static ExprIndex parse_expr_l2(TranslationUnit* tu, TokenStream* restrict s) {
	// TODO(NeGate): Convert this code into a loop... please?
	// TODO(NeGate): just rewrite this in general...
	SourceLocIndex loc = tokens_get(s)->location;
	
	if (tokens_get(s)->type == '*') {
		tokens_next(s);
		ExprIndex value = parse_expr_l2(tu, s);
		
		ExprIndex e = make_expr(tu);
		tu->exprs[e] = (Expr) {
			.op = EXPR_DEREF,
			.loc = tokens_get(s)->location,
			.unary_op.src = value
		};
		return e;
	} else if (tokens_get(s)->type == '!') {
		tokens_next(s);
		ExprIndex value = parse_expr_l2(tu, s);
		
		ExprIndex e = make_expr(tu);
		tu->exprs[e] = (Expr) {
			.op = EXPR_LOGICAL_NOT,
			.loc = loc,
			.unary_op.src = value
		};
		return e;
	} else if (tokens_get(s)->type == TOKEN_DOUBLE_EXCLAMATION) {
		tokens_next(s);
		ExprIndex value = parse_expr_l2(tu, s);
		
		ExprIndex e = make_expr(tu);
		tu->exprs[e] = (Expr) {
			.op = EXPR_CAST,
			.loc = loc,
			.cast = { TYPE_BOOL, value }
		};
		return e;
	} else if (tokens_get(s)->type == '-') {
		tokens_next(s);
		ExprIndex value = parse_expr_l2(tu, s);
		
		ExprIndex e = make_expr(tu);
		tu->exprs[e] = (Expr) {
			.op = EXPR_NEGATE,
			.loc = loc,
			.unary_op.src = value
		};
		return e;
	} else if (tokens_get(s)->type == '~') {
		tokens_next(s);
		ExprIndex value = parse_expr_l2(tu, s);
		
		ExprIndex e = make_expr(tu);
		tu->exprs[e] = (Expr) {
			.op = EXPR_NOT,
			.loc = loc,
			.unary_op.src = value
		};
		return e;
	} else if (tokens_get(s)->type == '+') {
		tokens_next(s);
		return parse_expr_l2(tu, s);
	} else if (tokens_get(s)->type == TOKEN_INCREMENT) {
		tokens_next(s);
		ExprIndex value = parse_expr_l1(tu, s);
		
		ExprIndex e = make_expr(tu);
		tu->exprs[e] = (Expr) {
			.op = EXPR_PRE_INC,
			.loc = loc,
			.unary_op.src = value
		};
		return e;
	} else if (tokens_get(s)->type == TOKEN_DECREMENT) {
		tokens_next(s);
		ExprIndex value = parse_expr_l1(tu, s);
		
		ExprIndex e = make_expr(tu);
		tu->exprs[e] = (Expr) {
			.op = EXPR_PRE_DEC,
			.loc = loc,
			.unary_op.src = value
		};
		return e;
	} else if (tokens_get(s)->type == TOKEN_KW_sizeof ||
			   tokens_get(s)->type == TOKEN_KW_Alignof) {
		SourceLocIndex loc = tokens_get(s)->location;
		TknType operation_type = tokens_get(s)->type;
		tokens_next(s);
		
		bool has_paren = false;
		SourceLoc* opening_loc = NULL;
		if (tokens_get(s)->type == '(') {
			has_paren = true;
			
			opening_loc = &s->line_arena[tokens_get(s)->location];
			tokens_next(s);
		}
		
		ExprIndex e = 0;
		if (is_typename(s)) {
			TypeIndex type = parse_typename(tu, s);
			
			if (has_paren) {
				expect_closing_paren(s, opening_loc);
			}
			
			// glorified backtracing on who own's the (
			// sizeof (int){ 0 } is a sizeof a compound list
			// not a sizeof(int) with a weird { 0 } laying around
			if (tokens_get(s)->type == '{') {
				tokens_next(s);
				
				e = parse_initializer(tu, s, type);
			} else {
				e = make_expr(tu);
				tu->exprs[e] = (Expr) {
					.op = operation_type == TOKEN_KW_sizeof ? EXPR_SIZEOF_T : EXPR_ALIGNOF_T,
					.loc = loc,
					.x_of_type = { type }
				};
			}
		} else {
			if (has_paren) tokens_prev(s);
			
			ExprIndex expr = parse_expr_l2(tu, s);
			
			e = make_expr(tu);
			tu->exprs[e] = (Expr) {
				.op = operation_type == TOKEN_KW_sizeof ? EXPR_SIZEOF : EXPR_ALIGNOF,
				.loc = loc,
				.x_of_expr = { expr }
			};
			
			/*if (has_paren) {
				expect_closing_paren(s, opening_loc);
			}*/
		}
		return e;
	} else if (tokens_get(s)->type == '&') {
		tokens_next(s);
		ExprIndex value = parse_expr_l1(tu, s);
		
		ExprIndex e = make_expr(tu);
		tu->exprs[e] = (Expr) {
			.op = EXPR_ADDR,
			.loc = loc,
			.unary_op.src = value
		};
		return e;
	} else {
		return parse_expr_l1(tu, s);
	}
}

// * / %
static ExprIndex parse_expr_l3(TranslationUnit* tu, TokenStream* restrict s) {
	ExprIndex lhs = parse_expr_l2(tu, s);
	
	while (tokens_get(s)->type == TOKEN_TIMES ||
		   tokens_get(s)->type == TOKEN_SLASH ||
		   tokens_get(s)->type == TOKEN_PERCENT) {
		ExprIndex e = make_expr(tu);
		ExprOp op;
		switch (tokens_get(s)->type) {
			case TOKEN_TIMES: op = EXPR_TIMES; break;
			case TOKEN_SLASH: op = EXPR_SLASH; break;
			case TOKEN_PERCENT: op = EXPR_PERCENT; break;
			default: __builtin_unreachable();
		}
		tokens_next(s);
		
		ExprIndex rhs = parse_expr_l2(tu, s);
		tu->exprs[e] = (Expr) {
			.op = op,
			.loc = tokens_get(s)->location,
			.bin_op = { lhs, rhs }
		};
		
		lhs = e;
	}
	
	return lhs;
}

// + -
static ExprIndex parse_expr_l4(TranslationUnit* tu, TokenStream* restrict s) {
	ExprIndex lhs = parse_expr_l3(tu, s);
	
	while (tokens_get(s)->type == TOKEN_PLUS ||
		   tokens_get(s)->type == TOKEN_MINUS) {
		SourceLocIndex loc = tokens_get(s)->location;
		ExprIndex e = make_expr(tu);
		ExprOp op;
		switch (tokens_get(s)->type) {
			case TOKEN_PLUS: op = EXPR_PLUS; break;
			case TOKEN_MINUS: op = EXPR_MINUS; break;
			default: __builtin_unreachable();
		}
		tokens_next(s);
		
		ExprIndex rhs = parse_expr_l3(tu, s);
		tu->exprs[e] = (Expr) {
			.op = op,
			.loc = loc,
			.bin_op = { lhs, rhs }
		};
		
		lhs = e;
	}
	
	return lhs;
}

// + -
static ExprIndex parse_expr_l5(TranslationUnit* tu, TokenStream* restrict s) {
	ExprIndex lhs = parse_expr_l4(tu, s);
	
	while (tokens_get(s)->type == TOKEN_LEFT_SHIFT ||
		   tokens_get(s)->type == TOKEN_RIGHT_SHIFT) {
		SourceLocIndex loc = tokens_get(s)->location;
		ExprIndex e = make_expr(tu);
		ExprOp op;
		switch (tokens_get(s)->type) {
			case TOKEN_LEFT_SHIFT: op = EXPR_SHL; break;
			case TOKEN_RIGHT_SHIFT: op = EXPR_SHR; break;
			default: __builtin_unreachable();
		}
		tokens_next(s);
		
		ExprIndex rhs = parse_expr_l4(tu, s);
		tu->exprs[e] = (Expr) {
			.op = op,
			.loc = loc,
			.bin_op = { lhs, rhs }
		};
		
		lhs = e;
	}
	
	return lhs;
}

// >= > <= <
static ExprIndex parse_expr_l6(TranslationUnit* tu, TokenStream* restrict s) {
	ExprIndex lhs = parse_expr_l5(tu, s);
	
	while (tokens_get(s)->type == TOKEN_GREATER_EQUAL ||
		   tokens_get(s)->type == TOKEN_LESS_EQUAL || 
		   tokens_get(s)->type == TOKEN_GREATER ||
		   tokens_get(s)->type == TOKEN_LESS) {
		SourceLocIndex loc = tokens_get(s)->location;
		ExprIndex e = make_expr(tu);
		ExprOp op;
		switch (tokens_get(s)->type) {
			case TOKEN_LESS:          op = EXPR_CMPLT; break;
			case TOKEN_LESS_EQUAL:    op = EXPR_CMPLE; break;
			case TOKEN_GREATER:       op = EXPR_CMPGT; break;
			case TOKEN_GREATER_EQUAL: op = EXPR_CMPGE; break;
			default: __builtin_unreachable();
		}
		tokens_next(s);
		
		ExprIndex rhs = parse_expr_l5(tu, s);
		tu->exprs[e] = (Expr) {
			.op = op,
			.loc = loc,
			.bin_op = { lhs, rhs }
		};
		
		lhs = e;
	}
	
	return lhs;
}

// == !=
static ExprIndex parse_expr_l7(TranslationUnit* tu, TokenStream* restrict s) {
	ExprIndex lhs = parse_expr_l6(tu, s);
	
	while (tokens_get(s)->type == TOKEN_NOT_EQUAL ||
		   tokens_get(s)->type == TOKEN_EQUALITY) {
		SourceLocIndex loc = tokens_get(s)->location;
		ExprIndex e = make_expr(tu);
		ExprOp op = tokens_get(s)->type == TOKEN_EQUALITY ? EXPR_CMPEQ : EXPR_CMPNE;
		tokens_next(s);
		
		ExprIndex rhs = parse_expr_l6(tu, s);
		tu->exprs[e] = (Expr) {
			.op = op,
			.loc = loc,
			.bin_op = { lhs, rhs }
		};
		
		lhs = e;
	}
	
	return lhs;
}

// &
static ExprIndex parse_expr_l8(TranslationUnit* tu, TokenStream* restrict s) {
	ExprIndex lhs = parse_expr_l7(tu, s);
	
	while (tokens_get(s)->type == '&') {
		SourceLocIndex loc = tokens_get(s)->location;
		ExprIndex e = make_expr(tu);
		ExprOp op = EXPR_AND;
		tokens_next(s);
		
		ExprIndex rhs = parse_expr_l7(tu, s);
		tu->exprs[e] = (Expr) {
			.op = op,
			.loc = loc,
			.bin_op = { lhs, rhs }
		};
		
		lhs = e;
	}
	
	return lhs;
}

// ^
static ExprIndex parse_expr_l9(TranslationUnit* tu, TokenStream* restrict s) {
	ExprIndex lhs = parse_expr_l8(tu, s);
	
	while (tokens_get(s)->type == '^') {
		SourceLocIndex loc = tokens_get(s)->location;
		ExprIndex e = make_expr(tu);
		ExprOp op = EXPR_XOR;
		tokens_next(s);
		
		ExprIndex rhs = parse_expr_l8(tu, s);
		tu->exprs[e] = (Expr) {
			.op = op,
			.loc = loc,
			.bin_op = { lhs, rhs }
		};
		
		lhs = e;
	}
	
	return lhs;
}

// |
static ExprIndex parse_expr_l10(TranslationUnit* tu, TokenStream* restrict s) {
	ExprIndex lhs = parse_expr_l9(tu, s);
	
	while (tokens_get(s)->type == '|') {
		SourceLocIndex loc = tokens_get(s)->location;
		ExprIndex e = make_expr(tu);
		ExprOp op = EXPR_OR;
		tokens_next(s);
		
		ExprIndex rhs = parse_expr_l9(tu, s);
		tu->exprs[e] = (Expr) {
			.op = op,
			.loc = loc,
			.bin_op = { lhs, rhs }
		};
		
		lhs = e;
	}
	
	return lhs;
}

// &&
static ExprIndex parse_expr_l11(TranslationUnit* tu, TokenStream* restrict s) {
	ExprIndex lhs = parse_expr_l10(tu, s);
	
	while (tokens_get(s)->type == TOKEN_DOUBLE_AND) {
		SourceLocIndex loc = tokens_get(s)->location;
		ExprIndex e = make_expr(tu);
		ExprOp op = EXPR_LOGICAL_AND;
		tokens_next(s);
		
		ExprIndex rhs = parse_expr_l10(tu, s);
		tu->exprs[e] = (Expr) {
			.op = op,
			.loc = loc,
			.bin_op = { lhs, rhs }
		};
		
		lhs = e;
	}
	
	return lhs;
}

// ||
static ExprIndex parse_expr_l12(TranslationUnit* tu, TokenStream* restrict s) {
	ExprIndex lhs = parse_expr_l11(tu, s);
	
	while (tokens_get(s)->type == TOKEN_DOUBLE_OR) {
		SourceLocIndex loc = tokens_get(s)->location;
		ExprIndex e = make_expr(tu);
		ExprOp op = EXPR_LOGICAL_OR;
		tokens_next(s);
		
		ExprIndex rhs = parse_expr_l11(tu, s);
		tu->exprs[e] = (Expr) {
			.op = op,
			.loc = loc,
			.bin_op = { lhs, rhs }
		};
		
		lhs = e;
	}
	
	return lhs;
}

// ternary
static ExprIndex parse_expr_l13(TranslationUnit* tu, TokenStream* restrict s) {
	ExprIndex lhs = parse_expr_l12(tu, s);
	
	if (tokens_get(s)->type == '?') {
		SourceLocIndex loc = tokens_get(s)->location;
		tokens_next(s);
		
		ExprIndex mhs = parse_expr(tu, s);
		
		expect(s, ':');
		
		ExprIndex rhs = parse_expr_l13(tu, s);
		
		ExprIndex e = make_expr(tu);
		tu->exprs[e] = (Expr) {
			.op = EXPR_TERNARY,
			.loc = loc,
			.ternary_op = { lhs, mhs, rhs }
		};
		
		return e;
	} else {
		return lhs;
	}
}

// = += -= *= /= %= <<= >>= &= ^= |=
//
// NOTE(NeGate): a=b=c is a=(b=c) not (a=b)=c
static ExprIndex parse_expr_l14(TranslationUnit* tu, TokenStream* restrict s) {
	ExprIndex lhs = parse_expr_l13(tu, s);
	
	if (tokens_get(s)->type == TOKEN_ASSIGN ||
		tokens_get(s)->type == TOKEN_PLUS_EQUAL ||
		tokens_get(s)->type == TOKEN_MINUS_EQUAL ||
		tokens_get(s)->type == TOKEN_TIMES_EQUAL ||
		tokens_get(s)->type == TOKEN_SLASH_EQUAL ||
		tokens_get(s)->type == TOKEN_PERCENT_EQUAL ||
		tokens_get(s)->type == TOKEN_AND_EQUAL ||
		tokens_get(s)->type == TOKEN_OR_EQUAL ||
		tokens_get(s)->type == TOKEN_XOR_EQUAL ||
		tokens_get(s)->type == TOKEN_LEFT_SHIFT_EQUAL ||
		tokens_get(s)->type == TOKEN_RIGHT_SHIFT_EQUAL) {
		SourceLocIndex loc = tokens_get(s)->location;
		ExprIndex e = make_expr(tu);
		
		ExprOp op;
		switch (tokens_get(s)->type) {
			case TOKEN_ASSIGN: op = EXPR_ASSIGN; break;
			case TOKEN_PLUS_EQUAL: op = EXPR_PLUS_ASSIGN; break;
			case TOKEN_MINUS_EQUAL: op = EXPR_MINUS_ASSIGN; break;
			case TOKEN_TIMES_EQUAL: op = EXPR_TIMES_ASSIGN; break;
			case TOKEN_SLASH_EQUAL: op = EXPR_SLASH_ASSIGN; break;
			case TOKEN_PERCENT_EQUAL: op = EXPR_PERCENT_ASSIGN; break;
			case TOKEN_AND_EQUAL: op = EXPR_AND_ASSIGN; break;
			case TOKEN_OR_EQUAL: op = EXPR_OR_ASSIGN; break;
			case TOKEN_XOR_EQUAL: op = EXPR_XOR_ASSIGN; break;
			case TOKEN_LEFT_SHIFT_EQUAL: op = EXPR_SHL_ASSIGN; break;
			case TOKEN_RIGHT_SHIFT_EQUAL: op = EXPR_SHR_ASSIGN; break;
			default: __builtin_unreachable();
		}
		tokens_next(s);
		
		ExprIndex rhs = parse_expr_l14(tu, s);
		tu->exprs[e] = (Expr) {
			.op = op,
			.loc = loc,
			.bin_op = { lhs, rhs }
		};
		return e;
	} else {
		return lhs;
	}
}

static ExprIndex parse_expr_l15(TranslationUnit* tu, TokenStream* restrict s) {
	ExprIndex lhs = parse_expr_l14(tu, s);
	
	while (tokens_get(s)->type == TOKEN_COMMA) {
		SourceLocIndex loc = tokens_get(s)->location;
		ExprIndex e = make_expr(tu);
		ExprOp op = EXPR_COMMA;
		tokens_next(s);
		
		ExprIndex rhs = parse_expr_l14(tu, s);
		tu->exprs[e] = (Expr) {
			.op = op,
			.loc = loc,
			.bin_op = { lhs, rhs }
		};
		
		lhs = e;
	}
	
	return lhs;
}

static ExprIndex parse_expr(TranslationUnit* tu, TokenStream* restrict s) {
	return parse_expr_l15(tu, s);
}