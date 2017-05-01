#include <assert.h>
#include <string.h>

#include "compiler.h"
#include "dynarr.h"

static token_t lookahead;

static token_t
consume (context_t *ctx)
{
	token_t result = lookahead;
	lookahead = scan(ctx);
	return result;
}

void
parser_init (context_t *ctx)
{
	lookahead = scan(ctx);
}

static expr_t*
alloc_expr (context_t *ctx, expr_type_t type)
{
	expr_t *expr = pool_alloc(&ctx->pool, sizeof(expr_t));
	expr->type = type;
	return expr;
}

static token_t
expect_token (context_t *ctx, token_type_t type)
{
	token_t t = consume(ctx);
	assert(t.type == type);
	return t;
}

static bool
is_binary_operator (token_type_t t)
{
	return t >= TOKEN_FIRST_BINARY_OPERATOR && t <= TOKEN_LAST_BINARY_OPERATOR;
}

static int
operator_precedence (token_type_t t)
{
	switch (t) {
		case TOKEN_TIMES:
			return 0;
		case TOKEN_PLUS:
			return 1;
		case TOKEN_LESS:
		case TOKEN_EQUALS:
			return 2;
		case TOKEN_LOGIC_AND:
		case TOKEN_LOGIC_OR:
			return 3;
		default:
			assert(false);
	}
}

static expr_t* parse_primary (context_t *ctx);

expr_t*
parse_expr (context_t *ctx)
{
	dynarr_t expr_arr;
	dynarr_t op_arr;

	dynarr_init(&expr_arr, &ctx->pool);
	dynarr_init(&op_arr, &ctx->pool);

	dynarr_append(&expr_arr, parse_primary(ctx));
	while (is_binary_operator(lookahead.type)) {
		dynarr_append(&op_arr, (void*)lookahead.type);
		consume(ctx);
		dynarr_append(&expr_arr, parse_primary(ctx));
	}

	for (int p = 0; p < 4; p++) {
		int i = 0;
		while (i < dynarr_length(&op_arr)) {
			token_type_t op = (token_type_t)dynarr_nth(&op_arr, i);
			if (operator_precedence(op) != p) {
				i++;
				continue;
			}

			expr_t *expr = alloc_expr(ctx, EXPR_BINARY);
			expr->v.binary.op = op;
			expr->v.binary.left = dynarr_nth(&expr_arr, i);
			expr->v.binary.right = dynarr_nth(&expr_arr, i + 1);

			dynarr_remove(&op_arr, i);
			dynarr_remove(&expr_arr, i);
			dynarr_set(&expr_arr, i, expr);
		}
	}

	assert(dynarr_length(&op_arr) == 0);
	assert(dynarr_length(&expr_arr) == 1);

	return dynarr_nth(&expr_arr, 0);
}

static expr_t*
parse_primary (context_t *ctx)
{
	token_t t = consume(ctx);
	expr_t *expr;

	switch (t.type) {
			//integer
		case TOKEN_INTEGER:
			expr = alloc_expr(ctx, EXPR_INTEGER);
			expr->v.i = t.v.i;
			break;

			//| ident
		case TOKEN_IDENT:
			expr = alloc_expr(ctx, EXPR_IDENT);
			expr->v.ident = t.v.name;
			break;

			//| "if" expr "then" expr "else" expr "end"
		case TOKEN_IF:
			expr = alloc_expr(ctx, EXPR_IF);
			expr->v.if_expr.condition = parse_expr(ctx);
			expect_token(ctx, TOKEN_THEN);
			expr->v.if_expr.consequent = parse_expr(ctx);
			expect_token(ctx, TOKEN_ELSE);
			expr->v.if_expr.alternative = parse_expr(ctx);
			expect_token(ctx, TOKEN_END);
			break;

			//| ("let" | "loop") bindings "in" expr "end"
		case TOKEN_LET:
		case TOKEN_LOOP: {
			expr_type_t expr_type = t.type == TOKEN_LET ? EXPR_LET : EXPR_LOOP;
			dynarr_t arr;
			dynarr_init(&arr, &ctx->pool);
			do {
				binding_t *binding = pool_alloc(&ctx->pool, sizeof(binding_t));

				t = expect_token(ctx, TOKEN_IDENT);
				binding->name = t.v.name;
				expect_token(ctx, TOKEN_ASSIGN);
				binding->expr = parse_expr(ctx);

				dynarr_append(&arr, binding);

				t = consume(ctx);

				assert(t.type == TOKEN_AND || t.type == TOKEN_IN);
			} while (t.type == TOKEN_AND);

			expr = alloc_expr(ctx, expr_type);

			expr->v.let_loop.body = parse_expr(ctx);
			expect_token(ctx, TOKEN_END);

			binding_t *bindings = pool_alloc(&ctx->pool, sizeof(binding_t) * dynarr_length(&arr));
			for (int i = 0; i < dynarr_length(&arr); i++)
				memcpy(&bindings[i], dynarr_nth(&arr, i), sizeof(binding_t));

			expr->v.let_loop.n = dynarr_length(&arr);
			expr->v.let_loop.bindings = bindings;

			break;
		}

			//| "recur" arg {arg}
			//arg = "(" expr ")"
		case TOKEN_RECUR: {
			dynarr_t arr;
			dynarr_init(&arr, &ctx->pool);
			do {
				expect_token(ctx, TOKEN_OPEN_PAREN);
				dynarr_append(&arr, parse_expr(ctx));
				expect_token(ctx, TOKEN_CLOSE_PAREN);
			} while (lookahead.type == TOKEN_OPEN_PAREN);
			expr = alloc_expr(ctx, EXPR_RECUR);
			expr->v.recur.n = dynarr_length(&arr);
			expr->v.recur.args = (expr_t**)dynarr_data(&arr);
			break;
		}

			//| unop primary
		case TOKEN_NOT:
		case TOKEN_NEGATE:
			expr = alloc_expr(ctx, EXPR_UNARY);
			expr->v.unary.op = t.type;
			expr->v.unary.operand = parse_primary(ctx);
			break;

			//| "(" expr ")"
		case TOKEN_OPEN_PAREN:
			expr = parse_expr(ctx);
			expect_token(ctx, TOKEN_CLOSE_PAREN);
			break;

		default:
			// FIXME: parse error!
			assert(false);
	}
	return expr;
}