extern "C" void ScnRootG3dProcTranslated(CpuContext* MKW_RESTRICT ctx)
{
    uint32_t cr0_0 = 0;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r1 = ctx->gpr[1];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r5 = ctx->gpr[5];
    uint32_t r6 = ctx->gpr[6];
    uint32_t r7 = ctx->gpr[7];
    uint32_t r8 = ctx->gpr[8];
    uint32_t r12 = ctx->gpr[12];
    uint32_t r31 = ctx->gpr[31];
    uint32_t cr = ctx->cr;
    uint32_t ctr = ctx->ctr;
    uint32_t xer = ctx->xer;

    goto loc_80258B20;

loc_80258B20:
{
    MemoryInline::FlatWriteRam32((r1 + -16), r1);
    r1 = (r1 + -16);
    r0 = ctx->lr;
}

loc_80258B2C:
{
    MemoryInline::FlatWriteRam32((r1 + 20), r0);
    MemoryInline::FlatWriteRam32((r1 + 12), r31);
    r31 = r3;
    if ((static_cast<uint32_t>(r4) >= static_cast<uint32_t>(9))) {
        goto loc_80258B5C;
    }
}

loc_80258B3C:
{
    r7 = (r4 + -1);
    r8 = 1;
    r0 = MemoryInline::FlatRead32((r3 + 204));
    r7 = PPC_Slw(static_cast<uint32_t>(r8), static_cast<uint32_t>(r7));
    r0 = (r7 & r0);
    if ((static_cast<int32_t>(r0) == static_cast<int32_t>(0))) {
        goto loc_80258B5C;
    }
}

loc_80258B54:
{
    r0 = 1;
    goto loc_80258B60;
}

loc_80258B5C:
{
    r0 = 0;
}

loc_80258B60:
{
}

loc_80258B64:
{
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(0))) {
        goto loc_80258BB4;
    }
}

loc_80258B68:
{
    r0 = (r4 + -65536);
    SetCRResident(cr, xer, 0, static_cast<uint32_t>(r0), static_cast<uint32_t>(1));
}

loc_80258B70:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_80258BAC;
    }
}

loc_80258B74:
{
    r3 = MemoryInline::FlatRead32((r3 + 10376));
    SetCRResident(cr, xer, 0, static_cast<uint32_t>(r3), static_cast<uint32_t>(r6));
}

loc_80258B7C:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_80258BAC;
    }
}

loc_80258B80:
{
    r12 = MemoryInline::FlatRead32(r3);
    r4 = 65536;
    r6 = r31;
    r5 = 0;
    r12 = MemoryInline::FlatRead32((r12 + 12));
    r4 = (r4 + 3);
    ctr = r12;
    ctx->lr = 0x80258BA0u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[8] = r8;
    ctx->gpr[12] = r12;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeIndirectCpuInContext(ctr, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r7 = ctx->gpr[7];
    r8 = ctx->gpr[8];
    r12 = ctx->gpr[12];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r0 = 0;
    MemoryInline::FlatWrite32((r31 + 10376), r0);
    goto loc_80258BB4;
}

loc_80258BAC:
{
    r3 = r31;
    ctx->lr = 0x80258BB4u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[8] = r8;
    ctx->gpr[12] = r12;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpuInContext<0x80257FC0u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r7 = ctx->gpr[7];
    r8 = ctx->gpr[8];
    r12 = ctx->gpr[12];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
}

loc_80258BB4:
{
    r0 = MemoryInline::FlatRead32((r1 + 20));
    r31 = MemoryInline::FlatRead32((r1 + 12));
    ctx->lr = r0;
    r1 = (r1 + 16);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[8] = r8;
    ctx->gpr[12] = r12;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    return;
}

}
