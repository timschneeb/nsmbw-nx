extern "C" void NW4R_G3D_AnmTranslation_Translated(CpuContext* MKW_RESTRICT ctx)
{
    uint32_t cr0_0 = 0;
    uint32_t r1_psq_tmp_0 = 0;
    uint32_t r1_psq_tmp_1 = 0;
    uint32_t r1_psq_tmp_2 = 0;
    uint8_t* guest_range_0 = nullptr;
    uint8_t* guest_range_1 = nullptr;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r1 = ctx->gpr[1];
    uint32_t r2 = ctx->gpr[2];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r5 = ctx->gpr[5];
    uint32_t r28 = ctx->gpr[28];
    uint32_t r29 = ctx->gpr[29];
    uint32_t r30 = ctx->gpr[30];
    uint32_t r31 = ctx->gpr[31];
    PPC_FPR f1 = ctx->fpr[1];
    PPC_FPR f29 = ctx->fpr[29];
    PPC_FPR f30 = ctx->fpr[30];
    PPC_FPR f31 = ctx->fpr[31];
    uint32_t cr = ctx->cr;
    uint32_t xer = ctx->xer;

    goto loc_80241020;

loc_80241020:
{
    guest_range_0 = MemoryInline::ResolveRangeHost((r1 + -80), 0, 88u, false, true);
    MemoryInline::WriteResolved32(guest_range_0, 0u, (r1 + -80), r1);
    r1 = (r1 + -80);
    r0 = ctx->lr;
    MemoryInline::WriteResolved32(guest_range_0, 84u, (r1 + 84), r0);
    MemoryInline::WriteResolvedFloat64(guest_range_0, 64u, (r1 + 64), f31.d);
    // psq_store w=0 quant=0 (using PPC_PsqSt)
    r1_psq_tmp_0 = (r1 + 72);
    PPC_PsqStStackInline<0u, 0u>(ctx, r1_psq_tmp_0, PPC_PsFromScalarInline(f31.d));
    MemoryInline::WriteResolvedFloat64(guest_range_0, 48u, (r1 + 48), f30.d);
    // psq_store w=0 quant=0 (using PPC_PsqSt)
    r1_psq_tmp_1 = (r1 + 56);
    PPC_PsqStStackInline<0u, 0u>(ctx, r1_psq_tmp_1, PPC_PsFromScalarInline(f30.d));
    MemoryInline::WriteResolvedFloat64(guest_range_0, 32u, (r1 + 32), f29.d);
    // psq_store w=0 quant=0 (using PPC_PsqSt)
    r1_psq_tmp_2 = (r1 + 40);
    PPC_PsqStStackInline<0u, 0u>(ctx, r1_psq_tmp_2, PPC_PsFromScalarInline(f29.d));
    f29.d = f1.d;
    MemoryInline::WriteResolved32(guest_range_0, 28u, (r1 + 28), r31);
    r31 = MemoryInline::FlatRead32((r4 + 4));
    MemoryInline::WriteResolved32(guest_range_0, 24u, (r1 + 24), r30);
    r30 = r5;
    MemoryInline::WriteResolved32(guest_range_0, 20u, (r1 + 20), r29);
    r29 = r4;
    MemoryInline::WriteResolved32(guest_range_0, 16u, (r1 + 16), r28);
    r28 = r3;
    r3 = (r31 & -1073741824);
}

loc_8024106C:
{
    if ((static_cast<int32_t>(r3) == static_cast<int32_t>(0))) {
        goto loc_80241098;
    }
}

loc_80241070:
{
    r0 = (r3 + -1073741824);
}

loc_80241078:
{
    if ((static_cast<uint32_t>(r0) == static_cast<uint32_t>(0))) {
        goto loc_802410AC;
    }
}

loc_8024107C:
{
    r0 = (r3 + -2147483648);
}

loc_80241084:
{
    if ((static_cast<uint32_t>(r0) == static_cast<uint32_t>(0))) {
        goto loc_8024112C;
    }
}

loc_80241088:
{
    r0 = (r3 + 1073741824);
}

loc_80241090:
{
    if ((static_cast<uint32_t>(r0) == static_cast<uint32_t>(0))) {
        goto loc_802411AC;
    }
}

loc_80241094:
{
    goto loc_8024122C;
}

loc_80241098:
{
    f31.d = MemoryInline::FlatReadFloat32(r5);
    r30 = (r5 + 12);
    f30.d = MemoryInline::FlatReadFloat32((r5 + 4));
    f1.d = MemoryInline::FlatReadFloat32((r5 + 8));
    goto loc_80241238;
}

loc_802410AC:
{
    r0 = (r31 & 524288);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_802410B0:
{
    r3 = r30;
    r30 = (r5 + 4);
    if (((cr & 0x20000000u) != 0)) {
        goto loc_802410C4;
    }
}

loc_802410BC:
{
    f1.d = MemoryInline::FlatReadFloat32(r3);
    goto loc_802410D0;
}

loc_802410C4:
{
    r0 = MemoryInline::FlatRead32(r3);
    r3 = (r0 + r4);
    ctx->lr = 0x802410D0u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->fpr[1] = f1;
    ctx->fpr[29] = f29;
    ctx->fpr[30] = f30;
    ctx->fpr[31] = f31;
    ctx->cr = cr;
    InvokeDirectCpu<0x802403C0u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r2 = ctx->gpr[2];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    f1 = ctx->fpr[1];
    f29 = ctx->fpr[29];
    f30 = ctx->fpr[30];
    f31 = ctx->fpr[31];
    cr = ctx->cr;
    xer = ctx->xer;
}

loc_802410D0:
{
    r0 = (r31 & 1048576);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_802410D4:
{
    f31.d = f1.d;
    r3 = r30;
    r30 = (r30 + 4);
    if (((cr & 0x20000000u) != 0)) {
        goto loc_802410EC;
    }
}

loc_802410E4:
{
    f1.d = MemoryInline::FlatReadFloat32(r3);
    goto loc_802410FC;
}

loc_802410EC:
{
    r0 = MemoryInline::FlatRead32(r3);
    f1.d = f29.d;
    r3 = (r0 + r29);
    ctx->lr = 0x802410FCu;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->fpr[1] = f1;
    ctx->fpr[29] = f29;
    ctx->fpr[30] = f30;
    ctx->fpr[31] = f31;
    ctx->cr = cr;
    InvokeDirectCpu<0x802403C0u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r2 = ctx->gpr[2];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    f1 = ctx->fpr[1];
    f29 = ctx->fpr[29];
    f30 = ctx->fpr[30];
    f31 = ctx->fpr[31];
    cr = ctx->cr;
    xer = ctx->xer;
}

loc_802410FC:
{
    r0 = (r31 & 2097152);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_80241100:
{
    f30.d = f1.d;
    r3 = r30;
    r30 = (r30 + 4);
    if (((cr & 0x20000000u) != 0)) {
        goto loc_80241118;
    }
}

loc_80241110:
{
    f1.d = MemoryInline::FlatReadFloat32(r3);
    goto loc_80241238;
}

loc_80241118:
{
    r0 = MemoryInline::FlatRead32(r3);
    f1.d = f29.d;
    r3 = (r0 + r29);
    ctx->lr = 0x80241128u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->fpr[1] = f1;
    ctx->fpr[29] = f29;
    ctx->fpr[30] = f30;
    ctx->fpr[31] = f31;
    ctx->cr = cr;
    InvokeDirectCpu<0x802403C0u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r2 = ctx->gpr[2];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    f1 = ctx->fpr[1];
    f29 = ctx->fpr[29];
    f30 = ctx->fpr[30];
    f31 = ctx->fpr[31];
    cr = ctx->cr;
    xer = ctx->xer;
    goto loc_80241238;
}

loc_8024112C:
{
    r0 = (r31 & 524288);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_80241130:
{
    r3 = r30;
    r30 = (r5 + 4);
    if (((cr & 0x20000000u) != 0)) {
        goto loc_80241144;
    }
}

loc_8024113C:
{
    f1.d = MemoryInline::FlatReadFloat32(r3);
    goto loc_80241150;
}

loc_80241144:
{
    r0 = MemoryInline::FlatRead32(r3);
    r3 = (r0 + r4);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->fpr[1] = f1;
    ctx->cr = cr;
    MKW_STATIC_TRANSLATED_CALL(0x80240240u, func_80240240, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    f1 = ctx->fpr[1];
    cr = ctx->cr;
}

loc_80241150:
{
    r0 = (r31 & 1048576);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_80241154:
{
    f31.d = f1.d;
    r3 = r30;
    r30 = (r30 + 4);
    if (((cr & 0x20000000u) != 0)) {
        goto loc_8024116C;
    }
}

loc_80241164:
{
    f1.d = MemoryInline::FlatReadFloat32(r3);
    goto loc_8024117C;
}

loc_8024116C:
{
    r0 = MemoryInline::FlatRead32(r3);
    f1.d = f29.d;
    r3 = (r0 + r29);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->fpr[1] = f1;
    ctx->cr = cr;
    MKW_STATIC_TRANSLATED_CALL(0x80240240u, func_80240240, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    f1 = ctx->fpr[1];
    cr = ctx->cr;
}

loc_8024117C:
{
    r0 = (r31 & 2097152);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_80241180:
{
    f30.d = f1.d;
    r3 = r30;
    r30 = (r30 + 4);
    if (((cr & 0x20000000u) != 0)) {
        goto loc_80241198;
    }
}

loc_80241190:
{
    f1.d = MemoryInline::FlatReadFloat32(r3);
    goto loc_80241238;
}

loc_80241198:
{
    r0 = MemoryInline::FlatRead32(r3);
    f1.d = f29.d;
    r3 = (r0 + r29);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->fpr[1] = f1;
    ctx->cr = cr;
    MKW_STATIC_TRANSLATED_CALL(0x80240240u, func_80240240, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    f1 = ctx->fpr[1];
    cr = ctx->cr;
    goto loc_80241238;
}

loc_802411AC:
{
    r0 = (r31 & 524288);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_802411B0:
{
    r3 = r30;
    r30 = (r5 + 4);
    if (((cr & 0x20000000u) != 0)) {
        goto loc_802411C4;
    }
}

loc_802411BC:
{
    f1.d = MemoryInline::FlatReadFloat32(r3);
    goto loc_802411D0;
}

loc_802411C4:
{
    r0 = MemoryInline::FlatRead32(r3);
    r3 = (r0 + r4);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->fpr[1] = f1;
    ctx->cr = cr;
    MKW_STATIC_TRANSLATED_CALL(0x80240100u, func_80240100, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    f1 = ctx->fpr[1];
    cr = ctx->cr;
}

loc_802411D0:
{
    r0 = (r31 & 1048576);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_802411D4:
{
    f31.d = f1.d;
    r3 = r30;
    r30 = (r30 + 4);
    if (((cr & 0x20000000u) != 0)) {
        goto loc_802411EC;
    }
}

loc_802411E4:
{
    f1.d = MemoryInline::FlatReadFloat32(r3);
    goto loc_802411FC;
}

loc_802411EC:
{
    r0 = MemoryInline::FlatRead32(r3);
    f1.d = f29.d;
    r3 = (r0 + r29);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->fpr[1] = f1;
    ctx->cr = cr;
    MKW_STATIC_TRANSLATED_CALL(0x80240100u, func_80240100, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    f1 = ctx->fpr[1];
    cr = ctx->cr;
}

loc_802411FC:
{
    r0 = (r31 & 2097152);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_80241200:
{
    f30.d = f1.d;
    r3 = r30;
    r30 = (r30 + 4);
    if (((cr & 0x20000000u) != 0)) {
        goto loc_80241218;
    }
}

loc_80241210:
{
    f1.d = MemoryInline::FlatReadFloat32(r3);
    goto loc_80241238;
}

loc_80241218:
{
    r0 = MemoryInline::FlatRead32(r3);
    f1.d = f29.d;
    r3 = (r0 + r29);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->fpr[1] = f1;
    ctx->cr = cr;
    MKW_STATIC_TRANSLATED_CALL(0x80240100u, func_80240100, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    f1 = ctx->fpr[1];
    cr = ctx->cr;
    goto loc_80241238;
}

loc_8024122C:
{
    f31.d = MemoryInline::FlatReadFloat32((r2 + -19176));
    f30.d = f31.d;
    f1.d = f31.d;
}

loc_80241238:
{
    MemoryInline::FlatWriteFloat32(r28, f31.d);
    r3 = r30;
    MemoryInline::FlatWriteFloat32((r28 + 4), f30.d);
    MemoryInline::FlatWriteFloat32((r28 + 8), f1.d);
    // psq_load w=0 quant=0 (using PPC_PsqL)
    guest_range_1 = MemoryInline::ResolveRangeHost((r1 + 16), 0, 72u, true, false);
    f31.d = MemoryInline::ReadResolvedFloat64(guest_range_1, 48u, (r1 + 64));
    // psq_load w=0 quant=0 (using PPC_PsqL)
    f30.d = MemoryInline::ReadResolvedFloat64(guest_range_1, 32u, (r1 + 48));
    // psq_load w=0 quant=0 (using PPC_PsqL)
    f29.d = MemoryInline::ReadResolvedFloat64(guest_range_1, 16u, (r1 + 32));
    {
        const auto resolved_pair = MemoryInline::ReadResolvedPair32(guest_range_1, 8u);
        if (resolved_pair.valid) {
            r31 = resolved_pair.second;
            r30 = resolved_pair.first;
        } else {
            r31 = MemoryInline::ReadResolved32(guest_range_1, 12u, (r1 + 28));
            r30 = MemoryInline::ReadResolved32(guest_range_1, 8u, (r1 + 24));
        }
    }
    {
        const auto resolved_pair = MemoryInline::ReadResolvedPair32(guest_range_1, 0u);
        if (resolved_pair.valid) {
            r29 = resolved_pair.second;
            r28 = resolved_pair.first;
        } else {
            r29 = MemoryInline::ReadResolved32(guest_range_1, 4u, (r1 + 20));
            r28 = MemoryInline::ReadResolved32(guest_range_1, 0u, (r1 + 16));
        }
    }
    r0 = MemoryInline::ReadResolved32(guest_range_1, 68u, (r1 + 84));
    ctx->lr = r0;
    r1 = (r1 + 80);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->fpr[1] = f1;
    ctx->fpr[29] = f29;
    ctx->fpr[30] = f30;
    ctx->fpr[31] = f31;
    ctx->cr = cr;
    return;
}

}