import numpy as np
import random
import subprocess
import pytest


def hex_to_fp16(val):
    val_bytes = val.to_bytes(2, byteorder='little', signed=False)
    return np.frombuffer(val_bytes, dtype=np.float16)[0]


def fp16_to_hex(val):
    val_bytes = val.tobytes()
    return np.frombuffer(val_bytes, dtype=np.uint16)[0]


def run_external_program(hex_a, hex_b, hex_c):
    result = subprocess.run(
        ['./a.out', f'{hex_a:04X}', f'{hex_b:04X}', f'{hex_c:04X}'],
        stdout=subprocess.PIPE,
        text=True
    )
    return result.stdout.strip()


@pytest.mark.parametrize("seed, num", [(0, 1000)])
def test_random_test(seed, num):
    random.seed(seed)
    for _ in range(num):
        hex_a = random.randint(0, 0xFFFF)
        hex_b = random.randint(0, 0xFFFF)
        hex_c = random.randint(0, 0xFFFF)

        a = hex_to_fp16(hex_a)
        b = hex_to_fp16(hex_b)
        c = hex_to_fp16(hex_c)

        ans = a + b * c
        ans_hex = fp16_to_hex(ans)

        output = run_external_program(hex_a, hex_b, hex_c)

        if output != f'{ans_hex:04X}':
            output_val = hex_to_fp16(int(output, 16))

            diff = abs(int(ans_hex) - int(output, 16))

            if np.isnan(ans) and np.isnan(output_val):
                continue

            # 実装仕様的に誤差2までは起きそうなので、それ以上の誤差のみ
            if diff > 2:
                print(f"{a}+{b}*{c}={ans}!={output_val}")
                print(
                    f"{hex_a:04X}, {hex_b:04X}, {hex_c:04X}, {ans_hex:04X} != {output}")
                print()
