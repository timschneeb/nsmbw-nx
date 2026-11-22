extern "C" void SinFIdxTranslated(CpuContext* MKW_RESTRICT ctx)
{
    uint32_t addr_lfsx_80237C24_loc_0 = 0;
    uint32_t cr0_0 = 0;
    uint32_t r0_rot_0 = 0;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r1 = ctx->gpr[1];
    uint32_t r2 = ctx->gpr[2];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    PPC_FPR f0 = ctx->fpr[0];
    PPC_FPR f1 = ctx->fpr[1];
    PPC_FPR f2 = ctx->fpr[2];
    PPC_FPR f3 = ctx->fpr[3];
    PPC_FPR f4 = ctx->fpr[4];
    uint32_t cr = ctx->cr;

    goto loc_80237BD0;

loc_80237BD0:
{
    f3.d = std::fabs(f1.d);
    MemoryInline::FlatWriteRam32((r1 + -16), r1);
    r1 = (r1 + -16);
    f0.d = MemoryInline::FlatReadFloat32((r2 + -19424));
    goto loc_80237BE4;
}

loc_80237BE0:
{
    f3.d = static_cast<double>(PpcForceSingleValueInline(f3.d - f0.d));
}

loc_80237BE4:
{
    SetCRFloatResident(cr, 0, f3.d, f0.d);
}

loc_80237BE8:
{
    if (((cr & 0x40000000u) != 0)) {
        goto loc_80237BE0;
    }
}

loc_80237BEC:
{
    r3 = (r1 + 12);
    // psq_store w=1 quant=3 (using PPC_PsqSt)
    PPC_PsqStStackInline<1u, 3u>(ctx, r3, PPC_PsFromScalarInline(f3.d));
    r3 = (r1 + 8);
    r0 = MemoryInline::FlatRead16((r1 + 12));
    MemoryInline::FlatWriteRam16((r1 + 8), static_cast<uint16_t>(r0));
    // psq_load w=1 quant=3 (using PPC_PsqL)
    PpcSetPairedFprInline(f2, PPC_PsqLStackInline<1u, 3u>(ctx, r3));
    r4 = 0x802F0000u;
    r0_rot_0 = PpcRotl32Inline(static_cast<uint32_t>(r0), static_cast<uint32_t>(4));
    r0 = (r0_rot_0 & 4080);
    f0.d = MemoryInline::FlatReadFloat32((r2 + -19420));
    r4 = (r4 + 29856);
    f4.d = static_cast<double>(PpcForceSingleValueInline(f3.d - PPC_PsToScalarInline(f2.d)));
    r3 = (r4 + r0);
    SetCRFloatResident(cr, 0, f1.d, f0.d);
}

loc_80237C20:
{
    f3.d = MemoryInline::FlatReadFloat32((r3 + 8));
    addr_lfsx_80237C24_loc_0 = (r4 + r0);
    f2.d = MemoryInline::FlatReadFloat32(addr_lfsx_80237C24_loc_0);
    f0.d = PpcFmulsInline(f4.d, f3.d);
    f1.d = static_cast<double>(PpcForceSingleValueInline(f2.d + f0.d));
    if (((cr & 0x80000000u) == 0)) {
        goto loc_80237C38;
    }
}

loc_80237C34:
{
    f1.d = (-(f1.d));
}

loc_80237C38:
{
    r1 = (r1 + 16);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->fpr[3] = f3;
    ctx->fpr[4] = f4;
    ctx->cr = cr;
    return;
}

}

extern "C" void CosFIdxTranslated(CpuContext* MKW_RESTRICT ctx)
{
    uint32_t cr0_0 = 0;
    uint32_t r0_rot_0 = 0;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r1 = ctx->gpr[1];
    uint32_t r2 = ctx->gpr[2];
    uint32_t r3 = ctx->gpr[3];
    PPC_FPR f0 = ctx->fpr[0];
    PPC_FPR f1 = ctx->fpr[1];
    PPC_FPR f2 = ctx->fpr[2];
    uint32_t cr = ctx->cr;

    goto loc_80237C40;

loc_80237C40:
{
    f1.d = std::fabs(f1.d);
    MemoryInline::FlatWriteRam32((r1 + -16), r1);
    r1 = (r1 + -16);
    f0.d = MemoryInline::FlatReadFloat32((r2 + -19424));
    goto loc_80237C54;
}

loc_80237C50:
{
    f1.d = static_cast<double>(PpcForceSingleValueInline(f1.d - f0.d));
}

loc_80237C54:
{
    SetCRFloatResident(cr, 0, f1.d, f0.d);
}

loc_80237C58:
{
    if (((cr & 0x40000000u) != 0)) {
        goto loc_80237C50;
    }
}

loc_80237C5C:
{
    r3 = (r1 + 12);
    // psq_store w=1 quant=3 (using PPC_PsqSt)
    PPC_PsqStStackInline<1u, 3u>(ctx, r3, PPC_PsFromScalarInline(f1.d));
    r3 = (r1 + 8);
    r0 = MemoryInline::FlatRead16((r1 + 12));
    MemoryInline::FlatWriteRam16((r1 + 8), static_cast<uint16_t>(r0));
    // psq_load w=1 quant=3 (using PPC_PsqL)
    PpcSetPairedFprInline(f0, PPC_PsqLStackInline<1u, 3u>(ctx, r3));
    r3 = 0x802F0000u;
    r0_rot_0 = PpcRotl32Inline(static_cast<uint32_t>(r0), static_cast<uint32_t>(4));
    r0 = (r0_rot_0 & 4080);
    r3 = (r3 + 29856);
    f2.d = static_cast<double>(PpcForceSingleValueInline(f1.d - PPC_PsToScalarInline(f0.d)));
    r3 = (r3 + r0);
    f1.d = MemoryInline::FlatReadFloat32((r3 + 12));
    f0.d = MemoryInline::FlatReadFloat32((r3 + 4));
    f1.d = PpcFmulsInline(f2.d, f1.d);
    f1.d = static_cast<double>(PpcForceSingleValueInline(f0.d + f1.d));
    r1 = (r1 + 16);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->cr = cr;
    return;
}

}
