def f(n):
    niters = 0
    a, b, c = 0, 0, 0
    while niters < n:
        a += 1
        if niters >= 10:
            break
        b += 1
        if niters > 5:
            niters += 1
            continue
        c += 1
        niters += 1
    print([a, b, c, niters])

def g():
    niters = 0
    a, b, c = 0, 0, 0
    while True:
        a += 1
        if niters >= 10:
            break
        b += 1
        if niters > 5:
            niters += 1
            continue
        c += 1
        niters += 1
    print([a, b, c, niters])

f(100)
f(8)
f(5)
f(3)
g()
