#!/usr/bin/env python3
from decimal import Decimal, getcontext

getcontext().prec = 100

def _calx_print(x):
    s = '{:f}'.format(x)

    if ('e' in s) or ('E' in s):
        raise ValueError(f'got scientific notation: {repr(s)}')

    if '.' in s:
        print(s.rstrip('0'))
    else:
        print(s + '.')

def add(a, b):
    _calx_print(a + b)
    _calx_print(b + a)

def sub(a, b):
    _calx_print(a - b)
    _calx_print(b - a)

def addsub(a, b):
    add(a, b)
    sub(a, b)

def x(a, b):
    addsub(a, b)
    addsub(-a, b)
    addsub(a, -b)
    addsub(-a, -b)

def y(a, b):
    F_PARTS = ['0', '0.5', '0.0001220703125']
    for fa in F_PARTS:
        for fb in F_PARTS:
            x(a + Decimal(fa), b + Decimal(fb))

y(Decimal('0'), Decimal('1'))
y(Decimal('1'), Decimal('1'))
y(Decimal('1'), Decimal('1234567890'))
