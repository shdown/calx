[![Build Status](https://travis-ci.org/shdown/calx.svg?branch=master)](https://travis-ci.org/shdown/calx)

This is an attempt to make a modern replacement for `bc`, while preserving its best features, such
as big-decimal numbers and explicit support for interactivity in the language.

calx’s motto is: **“deterministic and predictable output, not forecasting weather on Mars.”**

# Building calx

You will need:
  * a GNU C-compatible compiler, such as GNU GCC or Clang;
  * CMake;
  * the GNU readline library.

To build calx, simply clone this repository and run the following in its root:
```
cmake -DCMAKE_BUILD_TYPE=Release . && make
```
This will build the `./calx` binary.

# Calx by example

## Values and types

Calx programs operate on *values*. Each value has a *type*, which can be one of the following:
  * nil;
  * flag;
  * number;
  * string;
  * list;
  * dict;
  * function;
  * weak reference.

## Expressions, statements, expression statements

There are *expressions* and *statements*.

Expression is when you compute the value of something; `2+2` and `f(x)` are expressions.

Statement is when you *do* something; `a:=2;`, `x+=1;`, `return 2+2;` and `if(x!=1){x*=2;}` are
statements.

Now, an *expression statement* is a statement consisting solely of an expression.

In other words, an *expression statement* is when an expression is followed by a semicolon.
Note that a fake semicolon is inserted in this context after a line break, a `}`, and the
end-of-file.

In calx, the value of an expression statement is not *simply* evaluated and then discarded, as, for
example, in C-family languages, but also gets printed. Let’s test it:
```
≈≈> 2+2   # <- a fake semicolon is inserted here by the lexer
4
```

## Scope, global and local variables

Calx has lexical, function-wide scope with hoisting (much like pre-ES6 JavaScript). However, unlike
JavaScript, it has no support for closures, although functions can be nested.

A global variable is declared or assigned to with the following syntax: `NAME = VALUE;`.
Let’s test it:
```
≈≈> var = 5
≈≈> var
5
```

Attempt to load the value of an undefined global variable throws a runtime error. Let’s test it:
```
≈≈> var2
Runtime error: undefined global 'var2'
Stack trace (most recent first):
>>> at (input):1:
var2
```

A local variable is declared with the following syntax: `NAME := VALUE;`.
A local variable always has function scope. Note here that any chunk (a program with body) is a
function with zero parameters, and each line in the interactive mode is compiled as a separate
chunk. Let’s test it:
```
≈≈> var2 := 9; var2
9
≈≈> var2
Runtime error: undefined global 'var2'
Stack trace (most recent first):
>>> at (input):1:
var2
```

Note that the `NAME = VALUE;` statement just assigns another value to an existing local variable if
such exists in the current scope.

## Functions

Functions are defined with the following syntax: `fun NAME(PARAMS) { BODY }`. Let’s test it:
```
≈≈> fun myfunc(x, y) { return 2*x*y }
≈≈> myfunc(5, 6)
60
```

Without an explicit `return` statement, a function returns `nil`. The `nil` value does not get
printed in the “expression statement” context, so you can declare “procedures” — functions that do
things as opposed to compute things — using this syntax as well.

Remember functions are just ordinary values; you can, for example, pass them to other functions
just as well as numbers. The `fun NAME` statement is just an assignment: it is equivalent to
`NAME = <newly_created_function>;`.

As a test, let's use the built-in `Dasm` function that prints opcode listing for a function:
```
≈≈> Dasm(myfunc)
       0 |          OP_FUNCTION  0, 1
       1 |        OP_LOAD_CONST  0, 0
       2 |        OP_LOAD_LOCAL  0, 0
       3 |               OP_AOP  12, 0
       4 |        OP_LOAD_LOCAL  0, 1
       5 |               OP_AOP  12, 0
       6 |            OP_RETURN  0, 0
       7 |        OP_LOAD_CONST  0, 1
       8 |            OP_RETURN  0, 0
```

### Is it possible to create a local function?

Yes:
```
≈≈> f := nil;  fun f(x) { return 2*x };  f(3)
6
≈≈> f(3)
Runtime error: undefined global 'f'
Stack trace (most recent first):
>>> at (input):1:
f(3)
```

## Operators and grouping

There are binary and unary operators. All operators have priority; binary operators also have
associativity (either left-to-right or right-to-left). Expression can be grouped with parentheses.

In the tables below, operations with higher priority are done first.

### List of binary operators

Associativity defaults to “left-to-right”; see “types” below for the meaning of the “type” column.

| Spelling | Meaning              | Type         | Priority | Associativity |
| -------- | -------------------- | ------------ | -------- | ------------- |
| `+`      | Sum                  | Rational     | 19       |               |
| `-`      | Difference           | Rational     | 19       |               |
| `*`      | Product              | Rational     | 20       |               |
| `/`      | Quotient             | Rational     | 20       |               |
| `**`     | Power                | Rational     | 21       | Right-to-left |
| `//`     | Quotient (integral)  | Integral     | 20       |               |
| `%`      | Remainder (integral) | Integral     | 20       |               |
| `\|`     | Bitwise “OR”         | Bitwise      | 13       |               |
| `&`      | Bitwise “AND”        | Bitwise      | 14       |               |
| `^`      | Bitwise “XOR”        | Bitwise      | 15       |               |
| `<<`     | Bitwise left shift   | Bitwise      | 18       |               |
| `>>`     | Bitwise right shift  | Bitwise      | 18       |               |
| `<`      | Less                 | Comparative  | 17       |               |
| `<=`     | Less or equals       | Comparative  | 17       |               |
| `>`      | Greater              | Comparative  | 17       |               |
| `>=`     | Greater or equals    | Comparative  | 17       |               |
| `~`      | Concatenation        | Omnivorous   | 10       |               |
| `\|\|`   | Logical “OR”         | Omnivorous   | 11       |               |
| `&&`     | Logical “AND”        | Omnivorous   | 12       |               |
| `==`     | Equals               | Omnivorous   | 16       |               |
| `!=`     | Not equals           | Omnivorous   | 16       |               |

#### Types

- **Rational**: expect both of their operands to be numbers; throw otherwise. Also throw if one of
the operands is not in the domain of operator: `/` throws if the right operand is zero; `**` throws
if the right operand is non-integer, or if the right operand is negative (it is assumed that
`0 ** 0` is `1`).

- **Integral**: expect both of their operands to be numbers; throw otherwise. Then, assuming the
left and right operands were `left` and `right`, correspondingly, the result is
`TO_INTEGER(OPERATION(TO_INTEGER(left), TO_INTEGER(right)))`. These operators throw if one of the
operands is not in the domain of the operator: `//` and `%` throw if `TO_INTEGER(right)` is zero.
See below for the definition of the `TO_INTEGER` function.

- **Bitwise**: expect both of their operands to be numbers; throw otherwise. Then, assuming the
left and right operands were `left` and `right`, correspondingly, the result is
`OPERATION(TO_UINT32(left), TO_UINT32(right))`. The left and right shift operations return zero if
`TO_UINT32(right) >= 32`. See below for the definition of the `TO_UINT32` function.

- **Comparative**: expect their operands to be “meaningfully comparable”; currently, this means that
they must either be both numbers, or be both strings. Throw otherwise.

- **Omnivorous**: work with operands of any types.

#### Discussion

The `/` operator is the only one whose result depends on the *scale* (see the section on it below);
other always return the exact result.

The concatenation operator, assuming its left and right operands were `left` and `right`,
correspondingly, returns `CONCATENATE_STRINGS(TO_STRING(left), TO_STRING(right))`. See below for the
definition of the `TO_STRING` function.

The logical “OR” operator, assuming its left and right operands were `left` and `right`, returns
`left` if `TO_FLAG(left)` is `true`; otherwise, it returns `right`.

The logical “AND” operator, assuming its left and right operands were `left` and `right`, returns
`left` if `TO_FLAG(left) ` is `false`; otherwise, it returns `right`.

The `==` and `!=` operators compare their operands in the following way:
  * if the values are of different types, they compare as “not equal”;
  * number, string, flag and nil values compare “by value”;
  * values of other types compare “by identity” (read “by pointer”).

### List of unary operators

| Spelling | Meaning              | Priority |
| -------- | -------------------- | -------- |
| `-`      | Negation             | 50       |
| `!`      | Logical “NOT”        | 50       |
| `@`      | Length               | 60       |

The negation operator expects its operand to be number; throws otherwise.

The logical “NOT” operator works with operand of any type. Assuming the operand was `x`, the result
is `BOOL_NOT(TO_FLAG(x))`. See below for the definition of the `TO_FLAG` function.

The length operator returns the length of a list, a dict, or a string; throws if the operand is
neither.

## Compound assignments

For each binary operator with spelling `<op>`, there is a corresponding *compound assignment*
statement with the following syntax: `LHS <op>= VALUE;`. For example, to add something to a
variable, use `NAME += VALUE;`.

## The scale

The scale can be thought of as a global variable that sets the precision of the result of `/`
(division) operator. It also sets the precision of the results of various built-in analytic
functions.

Precision means decimal places, and thus can not be negative.

Its value can be read via the following call: `Scale()`. It returns the current precision as a number.

Its value can be set via the following call: `Scale(n)`.

## Lists

Lists are lists of values. `[]`, `[12]`, `[12, 34]`, `[12, 34, 56]` expressions all create new lists
with the specified elements.

### Get element

To get an element of the list by index, use `list[index]` syntax. `index` must be number;
otherwise, this construct throws.

If `index >= 0` and {`TO_INTEGER(index)` is a valid list index}, then the result is the value behind that index.

Otherwise, the result is `nil`.

See below for the definition of the `TO_INTEGER` function.

Let’s test it:

```
≈≈> xs = [1, 2, 3]
≈≈> xs[0]
1
≈≈> xs[0.9]
1
≈≈> xs[1]
2
≈≈> xs[2]
3
≈≈> xs[3]
≈≈> xs[-1]
≈≈> xs[-0.1]
≈≈> xs["test"]
Runtime error: attempt to index list with string (expected number)
Stack trace (most recent first):
>>> at (input):1:
xs["test"]
```

### Set element/push element back

To set list element by index, use `list[index] = value` syntax. `index` must be number; otherwise,
this construct throws.

If `index >= 0` and {`TO_INTEGER(index)` is a valid list index}, then the value behind that index is altered.

If `TO_INTEGER(index)` equals to the size of the list, a new element is pushed to the back of the list.

Otherwise, this construct throws.

See below for the definition of the `TO_INTEGER` function.

### Get size

To get the size of the list, use the `@` operator:
```
≈≈> @[12, 34]
2
```

### Pop element

Use the built-in `Pop` function to pop the last element off the list:

```
≈≈> xs = [1, 2, 3]
≈≈> Pop(xs)
3
≈≈> Pop(xs)
2
≈≈> Pop(xs)
1
≈≈> Pop(xs)
Runtime error: the list is empty
Stack trace (most recent first):
>>> at (input):1:
Pop(xs)
```

## Dicts

Dicts are mappings from strings to values. `{}`, `{"x": 1}`, `{"x": 1, "y": 2}` expressions all
create new dicts with the specified entries.

### Get size

To get the size (the number of entries) of the dict, use the `@` operator:
```
≈≈> @{"a": 1, "b": 2}
2
```

### Get element

To get the value behind a key, use `dict[key]` syntax. `key` must be string;
otherwise, this construct throws. If there is no such key, the result is `nil`.

There is a shortcut notation for constant keys that are valid identifiers: `dict.ident` is
equivalent to `dict["ident"]`.

Let’s test it:

```
≈≈> d = {"key1": 1, "key2": true, "key3": "str"}
≈≈> d["key1"]
1
≈≈> d.key1
1
≈≈> d.key2
true
≈≈> d.key3
str
≈≈> d.key4
≈≈> d[0]
Runtime error: attempt to index dict with number (expected string)
Stack trace (most recent first):
>>> at (input):1:
d[0]
```

### Set element

To alter the value behind a key or insert a new entry, use `dict[key] = value` syntax. `key` must be
string; otherwise, this construct throws.

Let’s test it:

```
≈≈> d = {"key1": 1, "key2": true, "key3": "str"}
≈≈> d.key3 = 3
≈≈> d["key4"] = 4
≈≈> d.key3; d.key4
3
4
```

### Remove element

To remove an entry from a dict behind a key, use `RemoveKey(dict, key)`.
If there is no entry with the key given, this function does nothing.

Let’s test it:
```
≈≈> d = {"key1": 1, "key2": 2}
≈≈> RemoveKey(d, "key1")
≈≈> d
{"key2": 2}
≈≈> RemoveKey(d, "z")
≈≈> d
{"key2": 2}
```

### Iterate over keys

```
≈≈> d = {"key1": 1, "key2": true, "key3": "str"}
≈≈> for (k := NextKey(d, nil); k; k = NextKey(d, k)) { k ~ " => " ~ d[k] }
key3 => str
key2 => true
key1 => 1
```

Note that the order of keys is unspecified.

## Numbers

Numbers are just numbers, big-decimal and having both integer and fractional parts of potentially
unlimited length:
```
≈≈> 123456789123456789123456789123456789.0123456789012345678901234567890123456789
123456789123456789123456789123456789.0123456789012345678901234567890123456789
```

You can also optionally separate their digits with a single quote symbol:
```
≈≈> 1'000'000
1000000
```

## Strings

Strings are just immutable arrays of bytes. String literals must always use double quotes; single
quotes are not supported:

```
≈≈> "test"
test
```

### Escapes

The following escapes in string literals are supported:

| Spelling | C equivalent         |
| -------- | -------------------- |
| `\\`     | `\\`                 |
| `\a`     | `\a`                 |
| `\b`     | `\b`                 |
| `\e`     | `\033`               |
| `\f`     | `\f`                 |
| `\n`     | `\n`                 |
| `\r`     | `\r`                 |
| `\t`     | `\t`                 |
| `\v`     | `\v`                 |
| `\"`     | `\"`                 |
| `\0`     | `\0`                 |
| `\x##`   | `\x##`               |

where `##` means two hexadecimal digits (both lower- and uppercase letters are accepted).

### Get size

To get the size (the number of bytes) of the string, use the `@` operator:
```
≈≈> @"test"
4
≈≈> @"ш"
2
```

### Get byte

To get string byte by index, use `str[index]` syntax. `index` must be number;
otherwise, this construct throws.

If `index >= 0` and {`TO_INTEGER(index)` is a valid string index}, then the result is a single-byte
string.

Otherwise, the result is `nil`.

See below for the definition of the `TO_INTEGER` function.

Let’s test it:

```
≈≈> "abcde"[3]
d
≈≈> "abcde"[100]
≈≈> "abcde"["x"]
Runtime error: attempt to index string with string (expected number)
Stack trace (most recent first):
>>> at (input):1:
"abcde"["x"]
```

## Literals of other types

`nil` is the literal of nil type.

`true` and `false` are literals of flag type.

## Truthiness

We say the value `x` is truthy iff `TO_FLAG(x)` returns true.

See below for the definition of the `TO_FLAG` function.

## `if` statement

An `if` statement, in its base form, has the following syntax: `if (CONDITION) { BODY }`.
It executes `BODY` if `TO_FLAG(CONDITION)` is `true`.

Another form is has the following syntax: `if (CONDITION) { BODY_1 } else { BODY_2 }`.
It executes `BODY_1` if `TO_FLAG(CONDITION)` is `true`, and `BODY_2` otherwise.

After the `if` clause (and before the `else` clause, if any), any number of `elif` (meaning "else if") clauses may be inserted;
an `elif` clause has the following syntax: `elif (COND) { BODY }`. Such a clause means that, if the conditions of all the previous
`if`/`elif` clauses were false so that their bodies were not executed, then `COND` must be evaluated, and, if `TO_FLAG(COND)` is `true`,
`BODY` of this `elif` clause should be executed, and body of `else` clause, if any, should not.

Following is an example of an `if` statement with an `elif` clause:
```
≈≈> if (2+2 == 2) {
×⋅⋅⋅> "two"
×⋅⋅⋅> } elif (2+2 == 4) {
×⋅⋅⋅> "four"
×⋅⋅⋅> } else {
×⋅⋅⋅> "neither"
×⋅⋅⋅> }
four
```

Overall, the semantics of an `if` statement is the following:

1. Evaluate the condition of the `if` clause; if truthy, execute the body of the `if` clause and go
to step 4.

2. If has no more `elif` clauses, then go to step 4. Otherwise, evaluate the condition of the
next `elif` clause; if truthy, execute the body of that `elif` clause and go to step 4.
Otherwise, repeat step 2.

3. If an `else` clause is present, then execute its body.

4. The execution of the statement is done.

## `while` statement

The `while` statement has the following syntax: `while (CONDITION) { BODY }`.

Its semantics is the following:

1. Evaluate the condition. If truthy, then execute the body and repeat step 1.

2. The execution of the statement is done.

## `for` statement

The `for` statement has the following syntax: `for (PRE; COND; POST) { BODY }`.

In the notation above:
  * both `PRE` and `POST` must be “maybe-expr-or-assignment-type statements”, where a “maybe-expr-or-assignment-type statement” is either of:
    - an empty statement;
    - an expression statement;
    - an assignment statement, including a compound assignment statement;
  * `COND` must be either empty or an expression.

Its semantics is the following:

1. Execute the `PRE` statement.

2. If `COND` is present, then evaluate it and, if not truthy, go to step 6.

3. Execute `BODY`.

4. Execute the `POST` statement.

5. Go to step 2.

6. The execution of the statement is done.

## `return` statement

The `return EXPRESSION;` statement evaluates `EXPRESSION` and returns its value from the innermost
function.

The `return;` statement is equivalent to `return nil;`.

## `break` statement

The `break;` statement breaks out of the innermost `while` or `for` loop.

For `while` loop, it means the control flow jumps to the step 2; for `for` loop, it means the
control flow jumps to the step 6.

## `continue` statement

The `continue;` statement forces the next ieration of the innermost `while` or `for` loop.

For `while` loop, it means the control flow jumps to the step 1; for `for` loop, it means the
control flow jumps to the step 4.

## Semicolon insertion rules

A semicolon inserted anywhere outside of an expression before:
 * a line break;
 * an end-of-input;
 * a `}` lexeme.

## Special functions used in this document

### `TO_INTEGER`

This function can only be applied to a number. It truncates the fractional part of the number;
in other words, it rounds the number towards zero.

### `TO_UINT32`

This function can only be applied to a number.

`TO_INT32(x)` returns `TO_INTEGER(x)` modulo `2 ** 32`; the result is always non-negative and less
than `2 ** 32`.

### `TO_STRING`

The behavior of this function depends on the type of its argument:

  * for string, it returns the value unmodified;
  * for number, it returns its string representation;
  * for flag, it returns either `"true"` or `"false"`;
  * for nil, it returns `"<nil>"`;
  * for list, it returns `"<list>"`;
  * for dict, it returns `"<dict>"`;
  * for function, it returns `"<function>"`;
  * for weak reference, it returns `"<weakref>"`.

### `TO_FLAG`

`TO_FLAG(x)` returns `false` if `x` is either `false` or `nil`; otherwise, it returns `true`.

## Built-in functions

### `Dasm`

`Dasm(f)` prints out the disassembly (bytecode listing) of a bytecode function `f`.

Returns `nil`.

### `Kind`

`Kind(v)` returns the name of the type of `v`:

  * for string, it returns `"string"`;
  * for number, it returns `"number"`;
  * for flag, it returns `"flag"`;
  * for nil, it returns `"nil"`;
  * for list, it returns `"list"`;
  * for dict, it returns `"dict"`;
  * for function, it returns `"function"`.
  * for weak reference, it returns `"weakref"`.

### `Pop`

`Pop(L)` pops an element from the back of the list `L` and returns the element.

Throws if `L` is empty.

### `Input`

`Input()` asks the user to enter a line and returns it as a string.

If the user refused, an empty string is returned.

### `Ord`

`Ord(c)`, where `c` is a one-byte string, returns the numeric value of that byte.

### `Chr`

`Chr(n)` returns a character (a single-byte string) by its numeric value `n`.

### `Error`

`Error(s)`, where `s` is a string, throws an error with message `s`.

### `RawRead`

`RawRead(s)`, where `s` is a (single-byte) string, behaves in either of the following ways,
depending on the value of `s`:

 * If `s` is `"L"`, then the function reads a line from stdin, and returns the line **with**
   the trailing line break, if any; on I/O error, or if the end-of-file is reached, returns an empty
   string.

 * If `s` is `"s"`, then the function reads a line from stdin, and returns the line **without** the
   trailing line break; on I/O error, or if the end-of-file is reached, returns an empty string.

 * If `s` is `"B"`, then the function reads a single byte from stdin, and returns a single-byte
   string with that byte; on I/O error, or if the end-of-file is reached, returns an empty string.

 * Otherwise, it throws.

### `RawWrite`

`RawWrite(s)`, where `s` is a string, prints `s` to stdout without trailing newline.

Returns `nil`.

### `Scale`

`Scale()` returns the current scale as a number.

`Scale(n)`, where `n` is a number, sets the current scale to `n`.

For what scale is, see the “The scale” section.

### `Where`

`Where()` prints out the stack trace.

Returns `nil`.

### `Random32`

`Random32()` returns a random 32-bit unsigned integer.

### `LoadString`

`LoadString(s)`, where `s` is a string, compiles `s` as a code, and returns the compiled function.

Throws if compilation fails.

For example, `Eval` in calx can be implemented as follows:
```
fun Eval(s) {
    return LoadString(s)()
}
```

### `Require`

`Require(s)`, where `s` is a string, tries to load and evaluate contents of file `<value of s>.calx`
from the directory `$CALX_PATH` (the value of `CALX_PATH` environment variable).

If `$CALX_PATH` was not defined or was empty, throws.

If `s` contains a prohibited character (including `/`, `.`, `\0`), throws.

### `NextKey`

`NextKey(d,k)`, where `d` is a dict and `k` is either string or nil, returns the "next" key
after `k` (or, if `k` is nil, the "first" key) in dict `d`; or, if there is no such ("next"/"first")
key, returns `nil`.

The total order on keys is defined on any version of a dict; it may potentially be changed every
time a dict is mutated.

### `RemoveKey`

`RemoveKey(d,k)`, where `d` is a dict and `k` is a string, removes the entry with key `k` from `d`,
if any.

### `ToNumber`

`ToNumber(s)`, where `s` is a string, parses `s` as a decimal number and returns the result.

Throws if the string cannot be parsed as decimal number.

### `Encode`

`Encode(x,b)` is equivalent to `Encode(x,b,0)`.

`Encode(x,b,n)`, where:
  * `x` is a number;
  * `b` is integer number such that `2 <= b <= 36`;
  * `n` is non-negative integer number,

returns the string representation of `x` in base `b` with `n` digits in the fractional part.

Note that not any decimal number has finite representation in any base; see, for example,
`Encode(0.1, 3, 100)`.

### `Decode`

`Decode(s,b)`, where:
  * `s` is a string;
  * `b` is integer number such that `2 <= b <= 36`,

parses `s` as a number in base `b`, truncating at `Scale()` decimal places (see the documentation
for the `Scale()` function).

Throws if `s` cannot be parsed as number in base `b`.

Note that not any number in any base has finite decimal representation; see, for example,
`Decode("0.1", 3)`.

### `NumDigits`

`NumDigits(x,s)`, where `x` is a number and `s` is a string, behaves in either of the following
ways, depending on the value of `s`:

  * If `s` is `"i"`, it returns the number of significant digits in the integer part of `x`.
  * If `s` is `"f"`, it returns the number of significant digits in the fractional part of `x`.
  * If `s` is `"+"`, it returns `NumDigits(x, "i") + NumDigits(x, "f")`.
  * Otherwise, it throws.

### `Wref`

`Wref(x)`, where `x` is a weakrefable value (currently, either list or dict value), returns a new
weak reference to `x`.

### `Wvalue`

`Wvalue(w)`, where `w` is a weak reference, returns the value behind the reference, or, if the value
has been garbage collected, returns `nil`.

### `Clock`

`Clock()` returns time, in seconds, since some fixed point in the past (before the start of the
program).

### `UpScale`

`UpScale(x,n)`, where `x` is a number and `n` is non-negative integer number, returns `x*(10**n)`.

### `DownScale`

`DownScale(x,n)`, where `x` is a number and `n` is non-negative integer number, returns `x/(10**n)`.

### `trunc`

`trunc(x)`, where `x` is a number, truncates the fractiotal part of `x`; in other words, it rounds
it towards zero.

### `floor`

`floor(x)`, where `x` is a number, rounds `x` towards negative infinity.

### `ceil`

`ceil(x)`, where `x` is a number, rounds `x` away from zero (towards infinity whose sign corresponds
to the sign of `x`).

### `round`

`round(x)`, where `x` is a number, rounds `x` to the nearest, ties away from zero.

### `frac`

`frac(x)`, where `x` is a number, returns the fractional part of `x`. Formally, for
non-negative `x`, it returns `x - trunc(x)`; for negative `x`, it returns `x + trunc(x)`.

### `ToString`

The behavior of `ToString(x)` is equivalent to that of the special function `TO_STRING(x)`; see the
section on special functions for more information.

### `Assert`

`Assert(cond)` throws if `!cond`.

### `abs`

`abs(x)`, where `x` is a number, returns the absolute value of `x`.

### `mod`

`mod(x,y)`, where `x` and `y` are integer numbers, `y > 0`, returns the
“canonical” representation of x modulo y: the integer in `[0; y)` congruent to `x` modulo `y`.

### `div_ceil`

`div_ceil(a,b)`, where `a`,`b` are non-negative integers, `b` is non-zero, returns
`a//b` if `(a%b)==0`, otherwise it returns `a//b + 1`.

### `fact`

`fact(n)`, where `n` is non-negative integer number, returns the factorial of `n`.

### `choice`

`choice(n,k)`, where `n` and `k` are non-negative integer numbers, returns `C(n,k)`.

### `fdiv`

`fdiv(x,y)`, where `x` and `y` are numbers, returns `trunc(x / y)`.

### `fmod`

`fmod(x,y)`, where `x` and `y` are numbers, returns `x - y * trunc(x / y)`.

### `gcd`

`gcd(u,v)`, where `u` and `v` are integer numbers, returns the greatest common divisor of `u` and `v`.

### `lcm`

`lcm(u,v)`, where `u` and `v` are integer numbers, returns the least common multiple of `u` and `v`.

### `mod_pow`

`mod_pow(b,e,m)`, where `b`,`e`,`m` are non-negative integers, `m` is non-zero, returns `(b**e)%m`.

### `random_bits`

`random_bits(n)`, where `n` is non-negative integer, returns a random number in `[0; 2**n-1]`.

### `random_mod`

`random_mod(n)`, where `n` is positive integer, returns a random number in `[0; n-1]`.

### `random_range`

`random_range(lb,rb)`, where `lb`,`rb` are integers, `lb<rb`, returns a random number in `[lb; rb-1]`.

### `probab_prime`

`probab_prime(x,nrounds)`, where `x` is integer, `nrounds` is non-negative integer, performs
some unspecified probabilistic primality tests. If `x` is prime, returns true; otherwise,
returns true with probability bounded by `4^(-nrounds)`, false otherwise.

### `jacobi`

`jacobi(a,n)`, where `a` is integer, `n` is positive odd integer, returns the value of Jacobi
symbol (a/n).

### `kronecker`

`kronecker(a,n)`, where `a`,`n` are integers, returns the value of Kronecker
symbol (a|n).

### `factorize`

`factorize(n)`, where `n` is positive integer, tries to find a non-trivial factor of `n`;
if it succeeds, it returns that factor; otherwise, it returns 0.

### `nth_root`

`nth_root(x,n)`, where `x` is a number, `n` is integer, `n>=2`,
returns the `n`-th root of `x`, rounded down, with precision
specified by the current scale (see the documentation on the `Scale()` function).

If `n` is even and `x` is negative, it throws.

### `sqrt`

`sqrt(x)` is equivalent to `nth_root(x,2)`.

### `cbrt`

`cbrt(x)` is equivalent to `nth_root(x,3)`.
