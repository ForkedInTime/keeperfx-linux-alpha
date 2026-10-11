// Standalone unit tests for the carry and rotate helpers of the software triangle
// rasteriser (src/kfx/renderer/software/bflib_render_trig.c).
//
// The rasteriser is a translation of 32-bit x86 code: texture coordinates step in
// 16.16 fixed point, and the carry out of bit 31 of a 32-bit add is what moves the
// texture V row on. On upstream's Windows build `long` is 4 bytes and that is what
// happens; with the helpers written on `long`, a 64-bit build never saw the carry
// (creature shadows and other textured polygons sampled the wrong rows).
//
// The helpers are static inline in a file that needs the whole engine to compile,
// so tests/run.sh cuts __CFADDL__ and __ROL4__ out of the real source into
// bin/render_trig_helpers.inc: this tests the code that ships, not a copy. The
// row-stepping case below mirrors the V step of trig_render_md10's pixel loop.
//
// Build and run: see tests/run.sh.
#include <cstdio>
#include <cstdint>

typedef unsigned int uint;  // as in src/bflib_basics.h, used by __ROL4__
typedef unsigned long ulong;
typedef unsigned short ushort;

#include "render_trig_helpers.inc"

static int g_failures = 0;

static void expect(bool ok, const char * what, unsigned long long got, unsigned long long wanted)
{
	if (ok) {
		std::printf("  ok   %s\n", what);
	} else {
		std::printf("  FAIL %s: got 0x%llx, wanted 0x%llx\n", what, got, wanted);
		++g_failures;
	}
}

static void expect_eq(unsigned long long got, unsigned long long wanted, const char * what)
{
	expect(got == wanted, what, got, wanted);
}

int main()
{
	// Carry out of bit 31, the flag the original's ADD set.
	expect_eq(__CFADDL__(0xFFFF0000u, 0x00020000u), 1, "0xFFFF0000 + 0x00020000 carries");
	expect_eq(__CFADDL__(0x80000000u, 0x80000000u), 1, "0x80000000 + 0x80000000 carries");
	expect_eq(__CFADDL__(0xFFFFFFFFu, 1u), 1, "0xFFFFFFFF + 1 carries");
	expect_eq(__CFADDL__(0xFFFFFFFFu, 0u), 0, "0xFFFFFFFF + 0 does not carry");
	expect_eq(__CFADDL__(0x7FFFFFFFu, 1u), 0, "0x7FFFFFFF + 1 does not carry (signed overflow only)");
	expect_eq(__CFADDL__(0x7FFF0000u, 0x7FFF0000u), 0, "0x7FFF0000 + 0x7FFF0000 does not carry");

	// The rasteriser passes 64-bit longs on Linux: v_step 1.5 (0x18000) shifted into the
	// high half is 0x1_8000_0000 there and 0x8000_0000 on Windows. Only the low 32 bits
	// may count.
	{
		long v_step_shifted = 0x18000L << 16;
		long accumulator = 0x80000000L;
		expect_eq(__CFADDL__(v_step_shifted, accumulator), 1,
			"64-bit operands: carry out of bit 31 of the low halves");
		long bit32 = 0x100000000L;
		expect_eq(__CFADDL__(bit32, bit32), 0,
			"64-bit operands: bits above 31 do not make a carry");
	}

	// Rotate by 16 swaps the 16.16 halves; the result is 32 bits wide.
	expect_eq(__ROL4__(0x12345678u, 16), 0x56781234u, "ROL4(0x12345678, 16)");
	expect_eq(__ROL4__(0xFFFF0001u, 16), 0x0001FFFFu, "ROL4(0xFFFF0001, 16) keeps no bits above 31");
	expect_eq(__ROL4__(0x12345678u, 8), 0x34567812u, "ROL4(0x12345678, 8)");
	expect_eq(__ROL4__(0x12345678u, -8), 0x78123456u, "ROL4(0x12345678, -8) rotates right");
	{
		long negative_v = -2; // V + step * x can go below zero before the rotate
		expect_eq(__ROL4__(negative_v, 16), 0xFFFEFFFFu, "ROL4 of a negative long is its low 32 bits rotated");
	}

	// trig_render_md10's V step: v_step 1.5 texels per pixel, starting on row 1.0.
	// Windows samples rows 1, 2, 4, 5, 7 -- the half-texel fraction carries every other
	// pixel. Without the carry the rows were 1, 2, 3, 4, 5.
	{
		long v_step = 0x18000;         // 1.5 in 16.16
		long u_step = 0;
		long V = 0x10000;              // row 1.0
		const uint32_t texture_v_step_fixed = v_step << 16;
		long factorA = __ROL4__(V, 16); // V fraction in the high half, row in the low half
		ushort colH = factorA;
		const unsigned wanted[] = {1, 2, 4, 5, 7};
		bool ok = true;
		unsigned got[5];
		for (int px = 0; px < 5; px++)
		{
			got[px] = colH & 0xFF;
			ok = ok && (got[px] == wanted[px]);
			factorA = (factorA & 0xFFFF0000) + ((u_step + factorA) & 0xFFFF);
			unsigned char factorA_carry = __CFADDL__(texture_v_step_fixed, factorA);
			factorA += texture_v_step_fixed;
			colH = colH + ((v_step >> 16) & 0xFF) + factorA_carry;
		}
		if (ok) {
			std::printf("  ok   v_step 1.5 samples rows 1, 2, 4, 5, 7\n");
		} else {
			std::printf("  FAIL v_step 1.5 sampled rows %u, %u, %u, %u, %u, wanted 1, 2, 4, 5, 7\n",
				got[0], got[1], got[2], got[3], got[4]);
			++g_failures;
		}
	}

	if (g_failures == 0) {
		std::printf("\nAll render trig carry tests passed.\n");
		return 0;
	}
	std::printf("\n%d render trig carry test(s) FAILED.\n", g_failures);
	return 1;
}
