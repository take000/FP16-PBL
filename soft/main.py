import numpy as np

def tobin(v):
    return f'{v.view(np.uint16):016b}'

a = np.float16(input("a="))
b = np.float16(input("b="))
c = np.float16(input("c="))

ans = a + b * c
print(ans, tobin(ans))

for i in range(4):
    print(hex(int(tobin(ans)[0+i*4:4+i*4], 2)), end=' ')
print()


print(f"(0b{tobin(a)}, 0b{tobin(b)}, 0b{tobin(c)}, &ans, 0)")
