#include "yamui_expr.h"

#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#define YUI_EXPR_MAX_STACK_DEPTH 32

static const char *TAG = "yamui_expr";

static char *yui_expr_strndup(const char *src, size_t len)
{
    if (!src) {
        return NULL;
    }
    char *copy = (char *)malloc(len + 1U);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, src, len);
    copy[len] = '\0';
    return copy;
}

void yui_expr_value_reset(yui_expr_value_t *value)
{
    if (!value) {
        return;
    }
    if (value->owned_string) {
        free(value->owned_string);
        value->owned_string = NULL;
    }
    value->string = NULL;
    value->number = 0.0;
    value->boolean = false;
    value->type = YUI_EXPR_VALUE_NULL;
}

void yui_expr_value_set_string_copy(yui_expr_value_t *value, const char *text)
{
    if (!value) {
        return;
    }
    yui_expr_value_reset(value);
    if (!text) {
        text = "";
    }
    size_t len = strlen(text);
    value->owned_string = yui_expr_strndup(text, len);
    if (!value->owned_string) {
        value->type = YUI_EXPR_VALUE_NULL;
        return;
    }
    value->string = value->owned_string;
    value->type = YUI_EXPR_VALUE_STRING;
}

void yui_expr_value_set_string_ref(yui_expr_value_t *value, const char *text)
{
    if (!value) {
        return;
    }
    yui_expr_value_reset(value);
    value->string = text ? text : "";
    value->type = YUI_EXPR_VALUE_STRING;
}

void yui_expr_value_set_number(yui_expr_value_t *value, double number)
{
    if (!value) {
        return;
    }
    yui_expr_value_reset(value);
    value->type = YUI_EXPR_VALUE_NUMBER;
    value->number = number;
}

void yui_expr_value_set_bool(yui_expr_value_t *value, bool flag)
{
    if (!value) {
        return;
    }
    yui_expr_value_reset(value);
    value->type = YUI_EXPR_VALUE_BOOL;
    value->boolean = flag;
}

static double yui_expr_as_number(const yui_expr_value_t *value)
{
    if (!value) {
        return 0.0;
    }
    switch (value->type) {
        case YUI_EXPR_VALUE_NUMBER:
            return value->number;
        case YUI_EXPR_VALUE_BOOL:
            return value->boolean ? 1.0 : 0.0;
        case YUI_EXPR_VALUE_STRING:
            if (!value->string || value->string[0] == '\0') {
                return 0.0;
            }
            return atof(value->string);
        default:
            return 0.0;
    }
}

static bool yui_expr_as_bool(const yui_expr_value_t *value)
{
    if (!value) {
        return false;
    }
    switch (value->type) {
        case YUI_EXPR_VALUE_BOOL:
            return value->boolean;
        case YUI_EXPR_VALUE_NUMBER:
            return fabs(value->number) > 1e-9;
        case YUI_EXPR_VALUE_STRING:
            return value->string && value->string[0] != '\0';
        default:
            return false;
    }
}

static const char *yui_expr_as_cstring(const yui_expr_value_t *value, char *scratch, size_t scratch_len)
{
    if (!value) {
        if (scratch && scratch_len > 0U) {
            scratch[0] = '\0';
        }
        return scratch;
    }
    switch (value->type) {
        case YUI_EXPR_VALUE_STRING:
            return value->string ? value->string : "";
        case YUI_EXPR_VALUE_NUMBER:
            if (scratch && scratch_len > 0U) {
                snprintf(scratch, scratch_len, "%.3f", value->number);
                size_t len = strlen(scratch);
                while (len > 0U && scratch[len - 1U] == '0') {
                    scratch[--len] = '\0';
                }
                if (len > 0U && scratch[len - 1U] == '.') {
                    scratch[len - 1U] = '\0';
                }
                return scratch;
            }
            return "";
        case YUI_EXPR_VALUE_BOOL:
            return value->boolean ? "true" : "false";
        default:
            return "";
    }
}

typedef enum {
    YUI_EXPR_TOKEN_EOF = 0,
    YUI_EXPR_TOKEN_ERROR,
    YUI_EXPR_TOKEN_IDENTIFIER,
    YUI_EXPR_TOKEN_NUMBER,
    YUI_EXPR_TOKEN_STRING,
    YUI_EXPR_TOKEN_TRUE,
    YUI_EXPR_TOKEN_FALSE,
    YUI_EXPR_TOKEN_NULL,
    YUI_EXPR_TOKEN_PLUS,
    YUI_EXPR_TOKEN_MINUS,
    YUI_EXPR_TOKEN_STAR,
    YUI_EXPR_TOKEN_SLASH,
    YUI_EXPR_TOKEN_LPAREN,
    YUI_EXPR_TOKEN_RPAREN,
    YUI_EXPR_TOKEN_BANG,
    YUI_EXPR_TOKEN_BANG_EQUAL,
    YUI_EXPR_TOKEN_EQUAL_EQUAL,
    YUI_EXPR_TOKEN_GREATER,
    YUI_EXPR_TOKEN_GREATER_EQUAL,
    YUI_EXPR_TOKEN_LESS,
    YUI_EXPR_TOKEN_LESS_EQUAL,
    YUI_EXPR_TOKEN_AND,
    YUI_EXPR_TOKEN_OR,
    YUI_EXPR_TOKEN_QUESTION,
    YUI_EXPR_TOKEN_COLON,
    YUI_EXPR_TOKEN_COALESCE,
} yui_expr_token_type_t;

typedef struct {
    yui_expr_token_type_t type;
    char *text;
    double number;
} yui_expr_token_t;

typedef struct {
    const char *input;
    const char *cursor;
} yui_expr_lexer_t;

typedef struct {
    yui_expr_lexer_t lexer;
    yui_expr_token_t current;
    yui_expr_symbol_resolver_t resolver;
    void *resolver_ctx;
    esp_err_t status;
} yui_expr_parser_t;

static void yui_expr_token_reset(yui_expr_token_t *token)
{
    if (!token) {
        return;
    }
    if (token->text) {
        free(token->text);
        token->text = NULL;
    }
    token->number = 0.0;
    token->type = YUI_EXPR_TOKEN_EOF;
}

static void yui_expr_lexer_init(yui_expr_lexer_t *lexer, const char *expression)
{
    lexer->input = expression ? expression : "";
    lexer->cursor = lexer->input;
}

static bool yui_expr_lexer_is_at_end(const yui_expr_lexer_t *lexer)
{
    return !lexer->cursor || *lexer->cursor == '\0';
}

static char yui_expr_lexer_peek(const yui_expr_lexer_t *lexer)
{
    if (yui_expr_lexer_is_at_end(lexer)) {
        return '\0';
    }
    return *lexer->cursor;
}

static char yui_expr_lexer_peek_next(const yui_expr_lexer_t *lexer)
{
    if (yui_expr_lexer_is_at_end(lexer)) {
        return '\0';
    }
    if (lexer->cursor[1] == '\0') {
        return '\0';
    }
    return lexer->cursor[1];
}

static char yui_expr_lexer_advance(yui_expr_lexer_t *lexer)
{
    if (yui_expr_lexer_is_at_end(lexer)) {
        return '\0';
    }
    char ch = *lexer->cursor;
    lexer->cursor++;
    return ch;
}

static void yui_expr_lexer_skip_whitespace(yui_expr_lexer_t *lexer)
{
    while (!yui_expr_lexer_is_at_end(lexer)) {
        char c = yui_expr_lexer_peek(lexer);
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            yui_expr_lexer_advance(lexer);
        } else {
            break;
        }
    }
}

static bool yui_expr_lexer_match(yui_expr_lexer_t *lexer, char expected)
{
    if (yui_expr_lexer_is_at_end(lexer)) {
        return false;
    }
    if (*lexer->cursor != expected) {
        return false;
    }
    lexer->cursor++;
    return true;
}

static yui_expr_token_t yui_expr_make_simple_token(yui_expr_token_type_t type)
{
    yui_expr_token_t token = {
        .type = type,
        .text = NULL,
        .number = 0.0,
    };
    return token;
}

static yui_expr_token_t yui_expr_scan_string(yui_expr_lexer_t *lexer, char quote)
{
    const char *start = lexer->cursor;
    size_t capacity = 16;
    size_t length = 0;
    char *buffer = (char *)malloc(capacity);
    if (!buffer) {
        return yui_expr_make_simple_token(YUI_EXPR_TOKEN_ERROR);
    }
    while (!yui_expr_lexer_is_at_end(lexer)) {
        char c = yui_expr_lexer_advance(lexer);
        if (c == quote) {
            buffer[length] = '\0';
            yui_expr_token_t token = {
                .type = YUI_EXPR_TOKEN_STRING,
                .text = buffer,
                .number = 0.0,
            };
            return token;
        }
        if (c == '\\' && !yui_expr_lexer_is_at_end(lexer)) {
            char next = yui_expr_lexer_advance(lexer);
            c = next;
            switch (next) {
                case 'n':
                    c = '\n';
                    break;
                case 't':
                    c = '\t';
                    break;
                case 'r':
                    c = '\r';
                    break;
                case '"':
                    c = '"';
                    break;
                case '\'':
                    c = '\'';
                    break;
                case '\\':
                default:
                    break;
            }
        }
        if (length + 1U >= capacity) {
            capacity *= 2U;
            char *resized = (char *)realloc(buffer, capacity);
            if (!resized) {
                free(buffer);
                return yui_expr_make_simple_token(YUI_EXPR_TOKEN_ERROR);
            }
            buffer = resized;
        }
        buffer[length++] = c;
    }
    free(buffer);
    (void)start;
    return yui_expr_make_simple_token(YUI_EXPR_TOKEN_ERROR);
}

static bool yui_expr_is_identifier_char(char c)
{
    return isalnum((unsigned char)c) || c == '_' || c == '.' || c == '-';
}

static yui_expr_token_t yui_expr_scan_identifier(yui_expr_lexer_t *lexer, char first_char)
{
    const char *start = lexer->cursor - 1;
    while (yui_expr_is_identifier_char(yui_expr_lexer_peek(lexer))) {
        yui_expr_lexer_advance(lexer);
    }
    size_t len = (size_t)(lexer->cursor - start);
    char *text = yui_expr_strndup(start, len);
    if (!text) {
        return yui_expr_make_simple_token(YUI_EXPR_TOKEN_ERROR);
    }
    if (strcasecmp(text, "true") == 0) {
        free(text);
        return yui_expr_make_simple_token(YUI_EXPR_TOKEN_TRUE);
    }
    if (strcasecmp(text, "false") == 0) {
        free(text);
        return yui_expr_make_simple_token(YUI_EXPR_TOKEN_FALSE);
    }
    if (strcasecmp(text, "null") == 0) {
        free(text);
        return yui_expr_make_simple_token(YUI_EXPR_TOKEN_NULL);
    }
    yui_expr_token_t token = {
        .type = YUI_EXPR_TOKEN_IDENTIFIER,
        .text = text,
        .number = 0.0,
    };
    return token;
}

static yui_expr_token_t yui_expr_scan_number(yui_expr_lexer_t *lexer, char first_char)
{
    const char *start = lexer->cursor - 1;
    bool has_dot = (first_char == '.');
    while (isdigit((unsigned char)yui_expr_lexer_peek(lexer)) || (!has_dot && yui_expr_lexer_peek(lexer) == '.')) {
        if (yui_expr_lexer_peek(lexer) == '.') {
            has_dot = true;
        }
        yui_expr_lexer_advance(lexer);
    }
    size_t len = (size_t)(lexer->cursor - start);
    char *text = yui_expr_strndup(start, len);
    if (!text) {
        return yui_expr_make_simple_token(YUI_EXPR_TOKEN_ERROR);
    }
    double value = atof(text);
    free(text);
    yui_expr_token_t token = {
        .type = YUI_EXPR_TOKEN_NUMBER,
        .text = NULL,
        .number = value,
    };
    return token;
}

static yui_expr_token_t yui_expr_lexer_next_token(yui_expr_lexer_t *lexer)
{
    yui_expr_lexer_skip_whitespace(lexer);
    if (yui_expr_lexer_is_at_end(lexer)) {
        return yui_expr_make_simple_token(YUI_EXPR_TOKEN_EOF);
    }
    char c = yui_expr_lexer_advance(lexer);
    if (isalpha((unsigned char)c) || c == '_' ) {
        return yui_expr_scan_identifier(lexer, c);
    }
    if (isdigit((unsigned char)c) || (c == '.' && isdigit((unsigned char)yui_expr_lexer_peek(lexer)))) {
        return yui_expr_scan_number(lexer, c);
    }
    switch (c) {
        case '"':
        case '\'':
            return yui_expr_scan_string(lexer, c);
        case '+':
            return yui_expr_make_simple_token(YUI_EXPR_TOKEN_PLUS);
        case '-':
            return yui_expr_make_simple_token(YUI_EXPR_TOKEN_MINUS);
        case '*':
            return yui_expr_make_simple_token(YUI_EXPR_TOKEN_STAR);
        case '/':
            return yui_expr_make_simple_token(YUI_EXPR_TOKEN_SLASH);
        case '(':
            return yui_expr_make_simple_token(YUI_EXPR_TOKEN_LPAREN);
        case ')':
            return yui_expr_make_simple_token(YUI_EXPR_TOKEN_RPAREN);
        case '!':
            if (yui_expr_lexer_match(lexer, '=')) {
                return yui_expr_make_simple_token(YUI_EXPR_TOKEN_BANG_EQUAL);
            }
            return yui_expr_make_simple_token(YUI_EXPR_TOKEN_BANG);
        case '=':
            if (yui_expr_lexer_match(lexer, '=')) {
                return yui_expr_make_simple_token(YUI_EXPR_TOKEN_EQUAL_EQUAL);
            }
            break;
        case '>':
            if (yui_expr_lexer_match(lexer, '=')) {
                return yui_expr_make_simple_token(YUI_EXPR_TOKEN_GREATER_EQUAL);
            }
            return yui_expr_make_simple_token(YUI_EXPR_TOKEN_GREATER);
        case '<':
            if (yui_expr_lexer_match(lexer, '=')) {
                return yui_expr_make_simple_token(YUI_EXPR_TOKEN_LESS_EQUAL);
            }
            return yui_expr_make_simple_token(YUI_EXPR_TOKEN_LESS);
        case '&':
            if (yui_expr_lexer_match(lexer, '&')) {
                return yui_expr_make_simple_token(YUI_EXPR_TOKEN_AND);
            }
            break;
        case '|':
            if (yui_expr_lexer_match(lexer, '|')) {
                return yui_expr_make_simple_token(YUI_EXPR_TOKEN_OR);
            }
            break;
        case '?':
            if (yui_expr_lexer_match(lexer, '?')) {
                return yui_expr_make_simple_token(YUI_EXPR_TOKEN_COALESCE);
            }
            return yui_expr_make_simple_token(YUI_EXPR_TOKEN_QUESTION);
        case ':':
            return yui_expr_make_simple_token(YUI_EXPR_TOKEN_COLON);
        default:
            break;
    }
    return yui_expr_make_simple_token(YUI_EXPR_TOKEN_ERROR);
}

static void yui_expr_parser_advance(yui_expr_parser_t *parser)
{
    yui_expr_token_reset(&parser->current);
    parser->current = yui_expr_lexer_next_token(&parser->lexer);
    if (parser->current.type == YUI_EXPR_TOKEN_ERROR) {
        parser->status = ESP_ERR_INVALID_ARG;
    }
}

static bool yui_expr_parser_expect(yui_expr_parser_t *parser, yui_expr_token_type_t type)
{
    if (parser->current.type == type) {
        yui_expr_parser_advance(parser);
        return true;
    }
    parser->status = ESP_ERR_INVALID_ARG;
    return false;
}

static yui_expr_value_t yui_expr_parse_expression(yui_expr_parser_t *parser);

static yui_expr_value_t yui_expr_make_null(void)
{
    yui_expr_value_t value = {
        .type = YUI_EXPR_VALUE_NULL,
        .number = 0.0,
        .boolean = false,
        .string = NULL,
        .owned_string = NULL,
    };
    return value;
}

static yui_expr_value_t yui_expr_parse_primary(yui_expr_parser_t *parser)
{
    if (parser->status != ESP_OK) {
        return yui_expr_make_null();
    }
    yui_expr_value_t value = yui_expr_make_null();
    switch (parser->current.type) {
        case YUI_EXPR_TOKEN_NUMBER:
            yui_expr_value_set_number(&value, parser->current.number);
            yui_expr_parser_advance(parser);
            return value;
        case YUI_EXPR_TOKEN_TRUE:
            yui_expr_value_set_bool(&value, true);
            yui_expr_parser_advance(parser);
            return value;
        case YUI_EXPR_TOKEN_FALSE:
            yui_expr_value_set_bool(&value, false);
            yui_expr_parser_advance(parser);
            return value;
        case YUI_EXPR_TOKEN_NULL:
            yui_expr_parser_advance(parser);
            return value;
        case YUI_EXPR_TOKEN_STRING:
            yui_expr_value_set_string_copy(&value, parser->current.text);
            yui_expr_parser_advance(parser);
            return value;
        case YUI_EXPR_TOKEN_IDENTIFIER: {
            const char *raw = parser->current.text ? parser->current.text : "";
            char *symbol = yui_expr_strndup(raw, strlen(raw));
            yui_expr_parser_advance(parser);
            if (!symbol) {
                parser->status = ESP_ERR_NO_MEM;
                return value;
            }
            if (parser->resolver && parser->resolver(symbol, parser->resolver_ctx, &value)) {
                /* resolver populated value */
            } else {
                yui_expr_value_set_string_ref(&value, "");
            }
            free(symbol);
            return value;
        }
        case YUI_EXPR_TOKEN_LPAREN:
            yui_expr_parser_advance(parser);
            value = yui_expr_parse_expression(parser);
            yui_expr_parser_expect(parser, YUI_EXPR_TOKEN_RPAREN);
            return value;
        default:
            parser->status = ESP_ERR_INVALID_ARG;
            return value;
    }
}

static yui_expr_value_t yui_expr_parse_unary(yui_expr_parser_t *parser)
{
    if (parser->status != ESP_OK) {
        return yui_expr_make_null();
    }
    if (parser->current.type == YUI_EXPR_TOKEN_BANG) {
        yui_expr_parser_advance(parser);
        yui_expr_value_t operand = yui_expr_parse_unary(parser);
        bool flag = !yui_expr_as_bool(&operand);
        yui_expr_value_reset(&operand);
        yui_expr_value_t result = yui_expr_make_null();
        yui_expr_value_set_bool(&result, flag);
        return result;
    }
    if (parser->current.type == YUI_EXPR_TOKEN_MINUS) {
        yui_expr_parser_advance(parser);
        yui_expr_value_t operand = yui_expr_parse_unary(parser);
        double number = -yui_expr_as_number(&operand);
        yui_expr_value_reset(&operand);
        yui_expr_value_t result = yui_expr_make_null();
        yui_expr_value_set_number(&result, number);
        return result;
    }
    return yui_expr_parse_primary(parser);
}

static yui_expr_value_t yui_expr_parse_factor(yui_expr_parser_t *parser)
{
    yui_expr_value_t value = yui_expr_parse_unary(parser);
    while (parser->status == ESP_OK && (parser->current.type == YUI_EXPR_TOKEN_STAR || parser->current.type == YUI_EXPR_TOKEN_SLASH)) {
        yui_expr_token_type_t type = parser->current.type;
        yui_expr_parser_advance(parser);
        yui_expr_value_t rhs = yui_expr_parse_unary(parser);
        double left = yui_expr_as_number(&value);
        double right = yui_expr_as_number(&rhs);
        double result = 0.0;
        if (type == YUI_EXPR_TOKEN_STAR) {
            result = left * right;
        } else if (right != 0.0) {
            result = left / right;
        } else {
            parser->status = ESP_ERR_INVALID_ARG;
        }
        yui_expr_value_reset(&value);
        yui_expr_value_set_number(&value, result);
        yui_expr_value_reset(&rhs);
    }
    return value;
}

static yui_expr_value_t yui_expr_parse_term(yui_expr_parser_t *parser)
{
    yui_expr_value_t value = yui_expr_parse_factor(parser);
    while (parser->status == ESP_OK && (parser->current.type == YUI_EXPR_TOKEN_PLUS || parser->current.type == YUI_EXPR_TOKEN_MINUS)) {
        yui_expr_token_type_t type = parser->current.type;
        yui_expr_parser_advance(parser);
        yui_expr_value_t rhs = yui_expr_parse_factor(parser);
        if (type == YUI_EXPR_TOKEN_PLUS && (value.type == YUI_EXPR_VALUE_STRING || rhs.type == YUI_EXPR_VALUE_STRING)) {
            char left_buf[64];
            char right_buf[64];
            const char *left = yui_expr_as_cstring(&value, left_buf, sizeof(left_buf));
            const char *right = yui_expr_as_cstring(&rhs, right_buf, sizeof(right_buf));
            size_t left_len = strlen(left);
            size_t right_len = strlen(right);
            size_t total = left_len + right_len;
            char *joined = (char *)malloc(total + 1U);
            if (!joined) {
                parser->status = ESP_ERR_NO_MEM;
            } else {
                memcpy(joined, left, left_len);
                memcpy(joined + left_len, right, right_len);
                joined[total] = '\0';
                yui_expr_value_reset(&value);
                value.type = YUI_EXPR_VALUE_STRING;
                value.owned_string = joined;
                value.string = joined;
            }
        } else {
            double left_num = yui_expr_as_number(&value);
            double right_num = yui_expr_as_number(&rhs);
            double result = (type == YUI_EXPR_TOKEN_PLUS) ? (left_num + right_num) : (left_num - right_num);
            yui_expr_value_reset(&value);
            yui_expr_value_set_number(&value, result);
        }
        yui_expr_value_reset(&rhs);
    }
    return value;
}

static yui_expr_value_t yui_expr_parse_comparison(yui_expr_parser_t *parser)
{
    yui_expr_value_t value = yui_expr_parse_term(parser);
    while (parser->status == ESP_OK && (parser->current.type == YUI_EXPR_TOKEN_GREATER || parser->current.type == YUI_EXPR_TOKEN_GREATER_EQUAL || parser->current.type == YUI_EXPR_TOKEN_LESS || parser->current.type == YUI_EXPR_TOKEN_LESS_EQUAL)) {
        yui_expr_token_type_t type = parser->current.type;
        yui_expr_parser_advance(parser);
        yui_expr_value_t rhs = yui_expr_parse_term(parser);
        double left = yui_expr_as_number(&value);
        double right = yui_expr_as_number(&rhs);
        bool result = false;
        switch (type) {
            case YUI_EXPR_TOKEN_GREATER:
                result = left > right;
                break;
            case YUI_EXPR_TOKEN_GREATER_EQUAL:
                result = left >= right;
                break;
            case YUI_EXPR_TOKEN_LESS:
                result = left < right;
                break;
            case YUI_EXPR_TOKEN_LESS_EQUAL:
                result = left <= right;
                break;
            default:
                break;
        }
        yui_expr_value_reset(&value);
        yui_expr_value_set_bool(&value, result);
        yui_expr_value_reset(&rhs);
    }
    return value;
}

static bool yui_expr_values_equal(const yui_expr_value_t *lhs, const yui_expr_value_t *rhs)
{
    if (lhs->type == YUI_EXPR_VALUE_STRING || rhs->type == YUI_EXPR_VALUE_STRING) {
        char left_buf[64];
        char right_buf[64];
        const char *l = yui_expr_as_cstring(lhs, left_buf, sizeof(left_buf));
        const char *r = yui_expr_as_cstring(rhs, right_buf, sizeof(right_buf));
        return strcmp(l, r) == 0;
    }
    if (lhs->type == YUI_EXPR_VALUE_BOOL || rhs->type == YUI_EXPR_VALUE_BOOL) {
        return yui_expr_as_bool(lhs) == yui_expr_as_bool(rhs);
    }
    return fabs(yui_expr_as_number(lhs) - yui_expr_as_number(rhs)) < 1e-6;
}

static yui_expr_value_t yui_expr_parse_equality(yui_expr_parser_t *parser)
{
    yui_expr_value_t value = yui_expr_parse_comparison(parser);
    while (parser->status == ESP_OK && (parser->current.type == YUI_EXPR_TOKEN_EQUAL_EQUAL || parser->current.type == YUI_EXPR_TOKEN_BANG_EQUAL)) {
        yui_expr_token_type_t type = parser->current.type;
        yui_expr_parser_advance(parser);
        yui_expr_value_t rhs = yui_expr_parse_comparison(parser);
        bool eq = yui_expr_values_equal(&value, &rhs);
        if (type == YUI_EXPR_TOKEN_BANG_EQUAL) {
            eq = !eq;
        }
        yui_expr_value_reset(&value);
        yui_expr_value_set_bool(&value, eq);
        yui_expr_value_reset(&rhs);
    }
    return value;
}

static yui_expr_value_t yui_expr_parse_and(yui_expr_parser_t *parser)
{
    yui_expr_value_t value = yui_expr_parse_equality(parser);
    while (parser->status == ESP_OK && parser->current.type == YUI_EXPR_TOKEN_AND) {
        yui_expr_parser_advance(parser);
        yui_expr_value_t rhs = yui_expr_parse_equality(parser);
        bool result = yui_expr_as_bool(&value) && yui_expr_as_bool(&rhs);
        yui_expr_value_reset(&value);
        yui_expr_value_set_bool(&value, result);
        yui_expr_value_reset(&rhs);
    }
    return value;
}

static yui_expr_value_t yui_expr_parse_or(yui_expr_parser_t *parser)
{
    yui_expr_value_t value = yui_expr_parse_and(parser);
    while (parser->status == ESP_OK && parser->current.type == YUI_EXPR_TOKEN_OR) {
        yui_expr_parser_advance(parser);
        yui_expr_value_t rhs = yui_expr_parse_and(parser);
        bool result = yui_expr_as_bool(&value) || yui_expr_as_bool(&rhs);
        yui_expr_value_reset(&value);
        yui_expr_value_set_bool(&value, result);
        yui_expr_value_reset(&rhs);
    }
    return value;
}

static yui_expr_value_t yui_expr_parse_coalesce(yui_expr_parser_t *parser)
{
    yui_expr_value_t value = yui_expr_parse_or(parser);
    while (parser->status == ESP_OK && parser->current.type == YUI_EXPR_TOKEN_COALESCE) {
        yui_expr_parser_advance(parser);
        if (value.type != YUI_EXPR_VALUE_NULL && !(value.type == YUI_EXPR_VALUE_STRING && (!value.string || value.string[0] == '\0'))) {
            yui_expr_value_t skipped = yui_expr_parse_or(parser);
            yui_expr_value_reset(&skipped);
            continue;
        }
        yui_expr_value_reset(&value);
        value = yui_expr_parse_or(parser);
    }
    return value;
}

static yui_expr_value_t yui_expr_parse_ternary(yui_expr_parser_t *parser)
{
    yui_expr_value_t condition = yui_expr_parse_coalesce(parser);
    if (parser->status == ESP_OK && parser->current.type == YUI_EXPR_TOKEN_QUESTION) {
        bool cond = yui_expr_as_bool(&condition);
        yui_expr_parser_advance(parser);
        yui_expr_value_t true_branch = yui_expr_parse_expression(parser);
        yui_expr_parser_expect(parser, YUI_EXPR_TOKEN_COLON);
        yui_expr_value_t false_branch = yui_expr_parse_expression(parser);
        yui_expr_value_reset(&condition);
        condition = cond ? true_branch : false_branch;
        if (cond) {
            yui_expr_value_reset(&false_branch);
        } else {
            yui_expr_value_reset(&true_branch);
        }
    }
    return condition;
}

static yui_expr_value_t yui_expr_parse_expression(yui_expr_parser_t *parser)
{
    return yui_expr_parse_ternary(parser);
}

esp_err_t yui_expr_eval(const char *expression, yui_expr_symbol_resolver_t resolver, void *ctx, yui_expr_value_t *out_value)
{
    if (!expression || !out_value) {
        return ESP_ERR_INVALID_ARG;
    }
    yui_expr_parser_t parser = {
        .resolver = resolver,
        .resolver_ctx = ctx,
        .status = ESP_OK,
    };
    yui_expr_lexer_init(&parser.lexer, expression);
    parser.current.type = YUI_EXPR_TOKEN_EOF;
    parser.current.text = NULL;
    parser.current.number = 0.0;
    yui_expr_parser_advance(&parser);
    yui_expr_value_t value = yui_expr_parse_expression(&parser);
    if (parser.status != ESP_OK) {
        ESP_LOGW(TAG, "Failed to evaluate expression '%s'", expression);
        yui_expr_value_reset(&value);
        yui_expr_token_reset(&parser.current);
        return parser.status;
    }
    *out_value = value;
    yui_expr_token_reset(&parser.current);
    return ESP_OK;
}

esp_err_t yui_expr_eval_to_string(const char *expression, yui_expr_symbol_resolver_t resolver, void *ctx, char *out, size_t out_len)
{
    if (!out || out_len == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    out[0] = '\0';
    yui_expr_value_t value = yui_expr_make_null();
    esp_err_t err = yui_expr_eval(expression, resolver, ctx, &value);
    if (err != ESP_OK) {
        yui_expr_value_reset(&value);
        return err;
    }
    char scratch[64];
    const char *text = yui_expr_as_cstring(&value, scratch, sizeof(scratch));
    if (!text) {
        text = "";
    }
    size_t len = strlen(text);
    if (len >= out_len) {
        len = out_len - 1U;
    }
    memcpy(out, text, len);
    out[len] = '\0';
    yui_expr_value_reset(&value);
    return ESP_OK;
}

esp_err_t yui_expr_collect_identifiers(const char *expression, yui_expr_identifier_cb_t cb, void *ctx)
{
    if (!expression || !cb) {
        return ESP_ERR_INVALID_ARG;
    }
    yui_expr_lexer_t lexer;
    yui_expr_lexer_init(&lexer, expression);
    while (true) {
        yui_expr_token_t token = yui_expr_lexer_next_token(&lexer);
        if (token.type == YUI_EXPR_TOKEN_ERROR) {
            if (token.text) {
                free(token.text);
            }
            return ESP_ERR_INVALID_ARG;
        }
        if (token.type == YUI_EXPR_TOKEN_IDENTIFIER && token.text) {
            cb(token.text, ctx);
        }
        if (token.text) {
            free(token.text);
        }
        if (token.type == YUI_EXPR_TOKEN_EOF) {
            break;
        }
    }
    return ESP_OK;
}
