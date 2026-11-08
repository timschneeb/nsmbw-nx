// Verbatim copy of the translator's output for 0x802403C0
// (nw4r::g3d::CalcAnimationFVS<CAnmFmtTraits<ResAnmChrFVS32Data>>), kept as the
// fallback for the GQR encodings the native fast path does not handle. The native
// override replaces the guest address, so emit-build-shards no longer generates
// this body.
void CalcAnimationFVS32Translated(CpuContext* MKW_RESTRICT ctx)
{
    uint32_t cr0_0 = 0;
    uint32_t r0_rot_0 = 0;
    uint32_t r0_rot_1 = 0;
    uint32_t r0_rot_2 = 0;
    uint32_t r0_rot_3 = 0;
    uint32_t r0_rot_4 = 0;
    uint32_t r0_rot_5 = 0;
    uint32_t r0_rot_6 = 0;
    uint32_t r0_rot_7 = 0;
    uint32_t r0_rot_8 = 0;
    uint32_t r0_rot_9 = 0;
    uint8_t* guest_range_0 = nullptr;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r1 = ctx->gpr[1];
    uint32_t r2 = ctx->gpr[2];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r5 = ctx->gpr[5];
    uint32_t r6 = ctx->gpr[6];
    uint32_t r7 = ctx->gpr[7];
    PPC_FPR f0 = ctx->fpr[0];
    PPC_FPR f1 = ctx->fpr[1];
    PPC_FPR f2 = ctx->fpr[2];
    PPC_FPR f3 = ctx->fpr[3];
    PPC_FPR f4 = ctx->fpr[4];
    PPC_FPR f5 = ctx->fpr[5];
    PPC_FPR f6 = ctx->fpr[6];
    PPC_FPR f7 = ctx->fpr[7];
    PPC_FPR f8 = ctx->fpr[8];
    PPC_FPR f9 = ctx->fpr[9];
    PPC_FPR f10 = ctx->fpr[10];
    PPC_FPR f11 = ctx->fpr[11];
    uint32_t cr = ctx->cr;
    uint32_t xer = ctx->xer;

    [[maybe_unused]] uint32_t mkw_gqr2 = ctx->gqr[2];

    goto loc_802403C0;

loc_802403C0:
{
    MemoryInline::FlatWriteRam32((r1 + -32), r1);
    r1 = (r1 + -32);
    r5 = (r3 + 16);
    guest_range_0 = MemoryInline::ResolveRangeHost(r3, 0, 16u, true, false);
    r6 = MemoryInline::ReadResolved16(guest_range_0, 0u, r3);
    r0 = (r6 + -1);
    r0_rot_0 = PpcRotl32Inline(static_cast<uint32_t>(r0), static_cast<uint32_t>(2));
    r0 = (r0_rot_0 & -4);
    r4 = (r3 + r0);
    r4 = (r4 + 16);
    // psq_load w=1 quant=2 (using PPC_PsqL)
    PpcSetPairedFprInline(f0, PPC_PsqLGqrInline<1u, 2u>(ctx, mkw_gqr2, r5));
    SetCRFloatResident(cr, 0, f1.d, PPC_PsToScalarInline(f0.d));
}

loc_802403E4:
{
    cr = PpcCrLogicalResident(cr, static_cast<uint32_t>(7), static_cast<uint32_t>(2), static_cast<uint32_t>(0), static_cast<uint32_t>(2));
    if (((cr & 0x20000000u) == 0)) {
        goto loc_80240410;
    }
}

loc_802403EC:
{
    r0 = MemoryInline::FlatRead32(r5);
    r4 = (r1 + 22);
    r0_rot_1 = PpcRotl32Inline(static_cast<uint32_t>(r0), static_cast<uint32_t>(20));
    r0 = (r0_rot_1 & 4095);
    MemoryInline::FlatWriteRam16((r1 + 22), static_cast<uint16_t>(r0));
    // psq_load w=1 quant=3 (using PPC_PsqL)
    PpcSetPairedFprInline(f2, PPC_PsqLStackInline<1u, 3u>(ctx, r4));
    {
        const auto resolved_pair = MemoryInline::ReadResolvedPair32(guest_range_0, 8u);
        if (resolved_pair.valid) {
            f1.d = PpcBitCastToFloatInline(resolved_pair.first);
            f0.d = PpcBitCastToFloatInline(resolved_pair.second);
        } else {
            f1.d = MemoryInline::ReadResolvedFloat32(guest_range_0, 8u, (r3 + 8));
            f0.d = MemoryInline::ReadResolvedFloat32(guest_range_0, 12u, (r3 + 12));
        }
    }
    f1.d = PPC_Fmadds(f1.d, PPC_PsToScalarInline(f2.d), f0.d);
    goto loc_802405A4;
}

loc_80240410:
{
    // psq_load w=1 quant=2 (using PPC_PsqL)
    PpcSetPairedFprInline(f0, PPC_PsqLGqrInline<1u, 2u>(ctx, mkw_gqr2, r4));
    SetCRFloatResident(cr, 0, PPC_PsToScalarInline(f0.d), f1.d);
}

loc_80240418:
{
    cr = PpcCrLogicalResident(cr, static_cast<uint32_t>(7), static_cast<uint32_t>(2), static_cast<uint32_t>(0), static_cast<uint32_t>(2));
    if (((cr & 0x20000000u) == 0)) {
        goto loc_80240444;
    }
}

loc_80240420:
{
    r0 = MemoryInline::FlatRead32(r4);
    r4 = (r1 + 20);
    r0_rot_2 = PpcRotl32Inline(static_cast<uint32_t>(r0), static_cast<uint32_t>(20));
    r0 = (r0_rot_2 & 4095);
    MemoryInline::FlatWriteRam16((r1 + 20), static_cast<uint16_t>(r0));
    // psq_load w=1 quant=3 (using PPC_PsqL)
    PpcSetPairedFprInline(f2, PPC_PsqLStackInline<1u, 3u>(ctx, r4));
    {
        const auto resolved_pair = MemoryInline::ReadResolvedPair32(guest_range_0, 8u);
        if (resolved_pair.valid) {
            f1.d = PpcBitCastToFloatInline(resolved_pair.first);
            f0.d = PpcBitCastToFloatInline(resolved_pair.second);
        } else {
            f1.d = MemoryInline::ReadResolvedFloat32(guest_range_0, 8u, (r3 + 8));
            f0.d = MemoryInline::ReadResolvedFloat32(guest_range_0, 12u, (r3 + 12));
        }
    }
    f1.d = PPC_Fmadds(f1.d, PPC_PsToScalarInline(f2.d), f0.d);
    goto loc_802405A4;
}

loc_80240444:
{
    // psq_load w=1 quant=2 (using PPC_PsqL)
    PpcSetPairedFprInline(f0, PPC_PsqLGqrInline<1u, 2u>(ctx, mkw_gqr2, r5));
    r4 = (r1 + 18);
    MemoryInline::FlatWriteRam16((r1 + 18), static_cast<uint16_t>(r6));
    f3.d = static_cast<double>(PpcForceSingleValueInline(f1.d - PPC_PsToScalarInline(f0.d)));
    // psq_load w=1 quant=3 (using PPC_PsqL)
    PpcSetPairedFprInline(f2, PPC_PsqLStackInline<1u, 3u>(ctx, r4));
    r4 = (r1 + 28);
    f0.d = MemoryInline::ReadResolvedFloat32(guest_range_0, 4u, (r3 + 4));
    f2.d = PpcFmulsInline(f3.d, PPC_PsToScalarInline(f2.d));
    f0.d = PpcFmulsInline(f0.d, f2.d);
    // psq_store w=1 quant=3 (using PPC_PsqSt)
    PPC_PsqStStackInline<1u, 3u>(ctx, r4, PPC_PsFromScalarInline(f0.d));
    r5 = (r1 + 24);
    r0 = MemoryInline::FlatRead16((r1 + 28));
    r0_rot_3 = PpcRotl32Inline(static_cast<uint32_t>(r0), static_cast<uint32_t>(2));
    r0 = (r0_rot_3 & -4);
    r4 = (r3 + r0);
    r7 = (r4 + 16);
    // psq_store w=1 quant=2 (using PPC_PsqSt)
    PPC_PsqStStackInline<1u, 2u>(ctx, r5, PPC_PsFromScalarInline(f1.d));
    r4 = MemoryInline::FlatRead8((r1 + 24));
    r0 = MemoryInline::FlatRead8(r7);
}

loc_80240490:
{
    if ((static_cast<uint32_t>(r4) >= static_cast<uint32_t>(r0))) {
        goto loc_802404A8;
    }
}

loc_80240494:
{
    // nop
}

loc_80240498:
{
    r7 = (r7 + -4);
    r0 = MemoryInline::FlatRead8(r7);
}

loc_802404A0:
{
    if ((static_cast<uint32_t>(r4) < static_cast<uint32_t>(r0))) {
        goto loc_80240498;
    }
}

loc_802404A4:
{
    goto loc_802404B8;
}

loc_802404A8:
{
    r7 = (r7 + 4);
    r0 = MemoryInline::FlatRead8(r7);
}

loc_802404B0:
{
    if ((static_cast<uint32_t>(r0) <= static_cast<uint32_t>(r4))) {
        goto loc_802404A8;
    }
}

loc_802404B4:
{
    r7 = (r7 + -4);
}

loc_802404B8:
{
    // psq_load w=1 quant=2 (using PPC_PsqL)
    PpcSetPairedFprInline(f0, PPC_PsqLGqrInline<1u, 2u>(ctx, mkw_gqr2, r7));
    SetCRFloatResident(cr, 0, f1.d, PPC_PsToScalarInline(f0.d));
}

loc_802404C0:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_802404E8;
    }
}

loc_802404C4:
{
    r0 = MemoryInline::FlatRead32(r7);
    r4 = (r1 + 16);
    r0_rot_4 = PpcRotl32Inline(static_cast<uint32_t>(r0), static_cast<uint32_t>(20));
    r0 = (r0_rot_4 & 4095);
    MemoryInline::FlatWriteRam16((r1 + 16), static_cast<uint16_t>(r0));
    // psq_load w=1 quant=3 (using PPC_PsqL)
    PpcSetPairedFprInline(f2, PPC_PsqLStackInline<1u, 3u>(ctx, r4));
    {
        const auto resolved_pair = MemoryInline::ReadResolvedPair32(guest_range_0, 8u);
        if (resolved_pair.valid) {
            f1.d = PpcBitCastToFloatInline(resolved_pair.first);
            f0.d = PpcBitCastToFloatInline(resolved_pair.second);
        } else {
            f1.d = MemoryInline::ReadResolvedFloat32(guest_range_0, 8u, (r3 + 8));
            f0.d = MemoryInline::ReadResolvedFloat32(guest_range_0, 12u, (r3 + 12));
        }
    }
    f1.d = PPC_Fmadds(f1.d, PPC_PsToScalarInline(f2.d), f0.d);
    goto loc_802405A4;
}

loc_802404E8:
{
    r4 = MemoryInline::FlatRead32(r7);
    r6 = (r1 + 14);
    r5 = (r7 + 4);
    r0_rot_5 = PpcRotl32Inline(static_cast<uint32_t>(r4), static_cast<uint32_t>(20));
    r0 = (r0_rot_5 & 4095);
    MemoryInline::FlatWriteRam16((r1 + 14), static_cast<uint16_t>(r0));
    // psq_load w=1 quant=3 (using PPC_PsqL)
    PpcSetPairedFprInline(f3, PPC_PsqLStackInline<1u, 3u>(ctx, r6));
    r0_rot_6 = PpcRotl32Inline(static_cast<uint32_t>(r4), static_cast<uint32_t>(20));
    r0 = (r0_rot_6 & -1048576);
    f2.d = MemoryInline::ReadResolvedFloat32(guest_range_0, 8u, (r3 + 8));
    r0 = (static_cast<int32_t>(r0) >> 20);
    f0.d = MemoryInline::ReadResolvedFloat32(guest_range_0, 12u, (r3 + 12));
    r3 = (r1 + 12);
    MemoryInline::FlatWriteRam16((r1 + 12), static_cast<uint16_t>(r0));
    f5.d = PPC_Fmadds(f2.d, PPC_PsToScalarInline(f3.d), f0.d);
    // psq_load w=1 quant=6 (using PPC_PsqL)
    PpcSetPairedFprInline(f6, PPC_PsqLStackInline<1u, 6u>(ctx, r3));
    r4 = (r1 + 10);
    r3 = MemoryInline::FlatRead32((r7 + 4));
    r0_rot_7 = PpcRotl32Inline(static_cast<uint32_t>(r3), static_cast<uint32_t>(20));
    r0 = (r0_rot_7 & 4095);
    MemoryInline::FlatWriteRam16((r1 + 10), static_cast<uint16_t>(r0));
    // psq_load w=1 quant=3 (using PPC_PsqL)
    PpcSetPairedFprInline(f3, PPC_PsqLStackInline<1u, 3u>(ctx, r4));
    r0_rot_8 = PpcRotl32Inline(static_cast<uint32_t>(r3), static_cast<uint32_t>(20));
    r0 = (r0_rot_8 & -1048576);
    {
        const uint32_t ppcCarryValue = static_cast<uint32_t>(r0);
        const uint32_t ppcCarryShift = static_cast<uint32_t>(20) & 0x3Fu;
        const bool ppcCarryNegative = (ppcCarryValue & 0x80000000u) != 0;
        const uint32_t ppcCarry = ppcCarryShift == 0 ? 0u : (ppcCarryShift >= 32 ? (ppcCarryNegative && ppcCarryValue != 0 ? 1u : 0u) : (ppcCarryNegative && (ppcCarryValue & ((1u << ppcCarryShift) - 1u)) != 0 ? 1u : 0u));
        xer = (xer & 0xDFFFFFFFu) | (ppcCarry << 29);
    }
    r0 = (static_cast<int32_t>(r0) >> 20);
    r3 = (r1 + 8);
    MemoryInline::FlatWriteRam16((r1 + 8), static_cast<uint16_t>(r0));
    f0.d = PPC_Fmadds(f2.d, PPC_PsToScalarInline(f3.d), f0.d);
    // psq_load w=1 quant=6 (using PPC_PsqL)
    PpcSetPairedFprInline(f7, PPC_PsqLStackInline<1u, 6u>(ctx, r3));
    // psq_load w=1 quant=2 (using PPC_PsqL)
    PpcSetPairedFprInline(f3, PPC_PsqLGqrInline<1u, 2u>(ctx, mkw_gqr2, r7));
    // psq_load w=1 quant=2 (using PPC_PsqL)
    PpcSetPairedFprInline(f4, PPC_PsqLGqrInline<1u, 2u>(ctx, mkw_gqr2, r5));
    f0.d = static_cast<double>(PpcForceSingleValueInline(f5.d - f0.d));
    f10.d = static_cast<double>(PpcForceSingleValueInline(f1.d - PPC_PsToScalarInline(f3.d)));
    f2.d = MemoryInline::FlatReadFloat32((r2 + -19168));
    f11.d = static_cast<double>(PpcForceSingleValueInline(PPC_PsToScalarInline(f4.d) - PPC_PsToScalarInline(f3.d)));
    f1.d = MemoryInline::FlatReadFloat32((r2 + -19164));
    f4.d = MemoryInline::FlatReadFloat32((r2 + -19172));
    PpcSetPairedFprInline(f9, PPC_Fres(PPC_PsFromScalarInline(f11.d)));
    PpcSetPairedFprInline(f8, PPC_PsAddInline(f9.d, f9.d));
    PpcSetPairedFprInline(f3, PPC_PsMulInline(f9.d, f9.d));
    PpcSetPairedFprInline(f9, PPC_PsNmsubInline(PPC_PsFromScalarInline(f11.d), f3.d, f8.d));
    f3.d = PpcFmulsInline(f10.d, PPC_PsToScalarInline(f9.d));
    f8.d = PPC_Fmsubs(f10.d, PPC_PsToScalarInline(f9.d), f4.d);
    f1.d = PPC_Fmsubs(f2.d, f3.d, f1.d);
    f2.d = PpcFmulsInline(f3.d, PPC_PsToScalarInline(f7.d));
    f4.d = PpcFmulsInline(f10.d, f8.d);
    f0.d = PpcFmulsInline(f1.d, f0.d);
    f1.d = PPC_Fmadds(f8.d, PPC_PsToScalarInline(f6.d), f2.d);
    f0.d = PpcFmulsInline(f3.d, f0.d);
    f0.d = PPC_Fmadds(f3.d, f0.d, f5.d);
    f1.d = PPC_Fmadds(f4.d, f1.d, f0.d);
}

loc_802405A4:
{
    r1 = (r1 + 32);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->fpr[3] = f3;
    ctx->fpr[4] = f4;
    ctx->fpr[5] = f5;
    ctx->fpr[6] = f6;
    ctx->fpr[7] = f7;
    ctx->fpr[8] = f8;
    ctx->fpr[9] = f9;
    ctx->fpr[10] = f10;
    ctx->fpr[11] = f11;
    ctx->cr = cr;
    ctx->xer = xer;
    return;
}

}
