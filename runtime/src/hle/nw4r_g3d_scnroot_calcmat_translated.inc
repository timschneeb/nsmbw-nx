extern "C" void ScnRootCalcMaterialTranslated(CpuContext* MKW_RESTRICT ctx)
{
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r5 = ctx->gpr[5];
    uint32_t r6 = ctx->gpr[6];
    uint32_t r12 = ctx->gpr[12];
    uint32_t ctr = ctx->ctr;

    goto loc_802590F0;

loc_802590F0:
{
    r12 = MemoryInline::FlatRead32(r3);
    r4 = 2;
    r5 = 0;
    r6 = 0;
    r12 = MemoryInline::FlatRead32((r12 + 12));
    ctr = r12;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[12] = r12;
    ctx->ctr = ctr;
    InvokeIndirectJumpInContext(ctr, ctx);
    return;
    return;
}

}
