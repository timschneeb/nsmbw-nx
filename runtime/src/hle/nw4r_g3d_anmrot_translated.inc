extern "C" void NW4R_G3D_AnmRotation_Translated(CpuContext* MKW_RESTRICT ctx)
{
    uint32_t addr_lfsx_80240E40_loc_0 = 0;
    uint32_t addr_lfsx_80240E58_loc_0 = 0;
    uint32_t addr_lfsx_80240E78_loc_0 = 0;
    uint32_t addr_lfsx_80240ED4_loc_0 = 0;
    uint32_t addr_lfsx_80240EEC_loc_0 = 0;
    uint32_t addr_lfsx_80240F0C_loc_0 = 0;
    uint32_t addr_lfsx_80240F68_loc_0 = 0;
    uint32_t addr_lfsx_80240F80_loc_0 = 0;
    uint32_t addr_lfsx_80240FA0_loc_0 = 0;
    uint32_t cr0_0 = 0;
    uint32_t r0_rot_0 = 0;
    uint32_t r0_rot_1 = 0;
    uint32_t r0_rot_2 = 0;
    uint32_t r0_rot_3 = 0;
    uint32_t r0_rot_4 = 0;
    uint32_t r0_rot_5 = 0;
    uint32_t r0_rot_6 = 0;
    uint32_t r0_rot_7 = 0;
    uint32_t r1_psq_tmp_0 = 0;
    uint32_t r1_psq_tmp_1 = 0;
    uint32_t r1_psq_tmp_2 = 0;
    uint32_t r3_rot_0 = 0;
    uint32_t r3_rot_1 = 0;
    uint32_t r3_rot_10 = 0;
    uint32_t r3_rot_11 = 0;
    uint32_t r3_rot_12 = 0;
    uint32_t r3_rot_13 = 0;
    uint32_t r3_rot_14 = 0;
    uint32_t r3_rot_15 = 0;
    uint32_t r3_rot_16 = 0;
    uint32_t r3_rot_17 = 0;
    uint32_t r3_rot_2 = 0;
    uint32_t r3_rot_3 = 0;
    uint32_t r3_rot_4 = 0;
    uint32_t r3_rot_5 = 0;
    uint32_t r3_rot_6 = 0;
    uint32_t r3_rot_7 = 0;
    uint32_t r3_rot_8 = 0;
    uint32_t r3_rot_9 = 0;
    uint8_t* guest_range_0 = nullptr;
    uint8_t* guest_range_1 = nullptr;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r1 = ctx->gpr[1];
    uint32_t r2 = ctx->gpr[2];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r5 = ctx->gpr[5];
    uint32_t r6 = ctx->gpr[6];
    uint32_t r7 = ctx->gpr[7];
    uint32_t r8 = ctx->gpr[8];
    uint32_t r11 = ctx->gpr[11];
    uint32_t r27 = ctx->gpr[27];
    uint32_t r28 = ctx->gpr[28];
    uint32_t r29 = ctx->gpr[29];
    uint32_t r30 = ctx->gpr[30];
    uint32_t r31 = ctx->gpr[31];
    PPC_FPR f0 = ctx->fpr[0];
    PPC_FPR f1 = ctx->fpr[1];
    PPC_FPR f2 = ctx->fpr[2];
    PPC_FPR f3 = ctx->fpr[3];
    PPC_FPR f4 = ctx->fpr[4];
    PPC_FPR f5 = ctx->fpr[5];
    PPC_FPR f6 = ctx->fpr[6];
    PPC_FPR f29 = ctx->fpr[29];
    PPC_FPR f30 = ctx->fpr[30];
    PPC_FPR f31 = ctx->fpr[31];
    uint32_t cr = ctx->cr;
    uint32_t xer = ctx->xer;

    goto loc_802405B0;

loc_802405B0:
{
    guest_range_0 = MemoryInline::ResolveRangeHost((r1 + -144), 0, 152u, false, true);
    MemoryInline::WriteResolved32(guest_range_0, 0u, (r1 + -144), r1);
    r1 = (r1 + -144);
    r0 = ctx->lr;
    MemoryInline::WriteResolved32(guest_range_0, 148u, (r1 + 148), r0);
    r11 = (r1 + 96);
    MemoryInline::WriteResolvedFloat64(guest_range_0, 128u, (r1 + 128), f31.d);
    // psq_store w=0 quant=0 (using PPC_PsqSt)
    r1_psq_tmp_0 = (r1 + 136);
    PPC_PsqStStackInline<0u, 0u>(ctx, r1_psq_tmp_0, PPC_PsFromScalarInline(f31.d));
    MemoryInline::WriteResolvedFloat64(guest_range_0, 112u, (r1 + 112), f30.d);
    // psq_store w=0 quant=0 (using PPC_PsqSt)
    r1_psq_tmp_1 = (r1 + 120);
    PPC_PsqStStackInline<0u, 0u>(ctx, r1_psq_tmp_1, PPC_PsFromScalarInline(f30.d));
    MemoryInline::WriteResolvedFloat64(guest_range_0, 96u, (r1 + 96), f29.d);
    // psq_store w=0 quant=0 (using PPC_PsqSt)
    r1_psq_tmp_2 = (r1 + 104);
    PPC_PsqStStackInline<0u, 0u>(ctx, r1_psq_tmp_2, PPC_PsFromScalarInline(f29.d));
    // inline leaf 0x802DCD74 (6 guest instruction(s))
    if (!MemoryInline::WriteResolvedPair32(guest_range_0, 76u, ((static_cast<uint64_t>(static_cast<uint32_t>(r27)) << 32) | static_cast<uint32_t>(r28)))) {
        MemoryInline::WriteResolved32(guest_range_0, 76u, (r11 + -20), r27);
        MemoryInline::WriteResolved32(guest_range_0, 80u, (r11 + -16), r28);
    }
    if (!MemoryInline::WriteResolvedPair32(guest_range_0, 84u, ((static_cast<uint64_t>(static_cast<uint32_t>(r29)) << 32) | static_cast<uint32_t>(r30)))) {
        MemoryInline::WriteResolved32(guest_range_0, 84u, (r11 + -12), r29);
        MemoryInline::WriteResolved32(guest_range_0, 88u, (r11 + -8), r30);
    }
    MemoryInline::WriteResolved32(guest_range_0, 92u, (r11 + -4), r31);
    // end of inlined leaf 0x802DCD74
    r31 = MemoryInline::FlatRead32((r6 + 4));
    r0 = 1127219200;
    f29.d = f1.d;
    MemoryInline::WriteResolved32(guest_range_0, 48u, (r1 + 48), r0);
    r8 = (r31 & 939524096);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r8), static_cast<int32_t>(0));
}

loc_802405F0:
{
    r27 = r3;
    MemoryInline::FlatWriteRam32((r1 + 56), r0);
    r28 = r4;
    r29 = r6;
    r30 = r7;
    if (((cr & 0x20000000u) != 0)) {
        goto loc_80240654;
    }
}

loc_80240608:
{
    r0 = (r8 + -134217728);
}

loc_80240610:
{
    if ((static_cast<uint32_t>(r0) == static_cast<uint32_t>(0))) {
        goto loc_80240668;
    }
}

loc_80240614:
{
    r0 = (r8 + -268435456);
}

loc_8024061C:
{
    if ((static_cast<uint32_t>(r0) == static_cast<uint32_t>(0))) {
        goto loc_802406EC;
    }
}

loc_80240620:
{
    r0 = (r8 + -402653184);
}

loc_80240628:
{
    if ((static_cast<uint32_t>(r0) == static_cast<uint32_t>(0))) {
        goto loc_80240770;
    }
}

loc_8024062C:
{
    r0 = (r8 + -536870912);
}

loc_80240634:
{
    if ((static_cast<uint32_t>(r0) == static_cast<uint32_t>(0))) {
        goto loc_802407F4;
    }
}

loc_80240638:
{
    r0 = (r8 + -671088640);
}

loc_80240640:
{
    if ((static_cast<uint32_t>(r0) == static_cast<uint32_t>(0))) {
        goto loc_80240AE4;
    }
}

loc_80240644:
{
    r0 = (r8 + -805306368);
    SetCRResident(cr, xer, 0, static_cast<uint32_t>(r0), static_cast<uint32_t>(0));
}

loc_8024064C:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_80240DEC;
    }
}

loc_80240650:
{
    goto loc_80240FB0;
}

loc_80240654:
{
    f31.d = MemoryInline::FlatReadFloat32(r7);
    r30 = (r7 + 12);
    f30.d = MemoryInline::FlatReadFloat32((r7 + 4));
    f29.d = MemoryInline::FlatReadFloat32((r7 + 8));
    goto loc_80240FBC;
}

loc_80240668:
{
    r0 = (r31 & 65536);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_8024066C:
{
    r3 = r30;
    r30 = (r7 + 4);
    if (((cr & 0x20000000u) != 0)) {
        goto loc_80240680;
    }
}

loc_80240678:
{
    f1.d = MemoryInline::FlatReadFloat32(r3);
    goto loc_8024068C;
}

loc_80240680:
{
    r0 = MemoryInline::FlatRead32(r3);
    r3 = (r0 + r6);
    ctx->lr = 0x8024068Cu;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[8] = r8;
    ctx->gpr[11] = r11;
    ctx->gpr[27] = r27;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->fpr[3] = f3;
    ctx->fpr[4] = f4;
    ctx->fpr[5] = f5;
    ctx->fpr[6] = f6;
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
    r6 = ctx->gpr[6];
    r7 = ctx->gpr[7];
    r8 = ctx->gpr[8];
    r11 = ctx->gpr[11];
    r27 = ctx->gpr[27];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    f0 = ctx->fpr[0];
    f1 = ctx->fpr[1];
    f2 = ctx->fpr[2];
    f3 = ctx->fpr[3];
    f4 = ctx->fpr[4];
    f5 = ctx->fpr[5];
    f6 = ctx->fpr[6];
    f29 = ctx->fpr[29];
    f30 = ctx->fpr[30];
    f31 = ctx->fpr[31];
    cr = ctx->cr;
    xer = ctx->xer;
}

loc_8024068C:
{
    r0 = (r31 & 131072);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_80240690:
{
    f31.d = f1.d;
    r3 = r30;
    r30 = (r30 + 4);
    if (((cr & 0x20000000u) != 0)) {
        goto loc_802406A8;
    }
}

loc_802406A0:
{
    f1.d = MemoryInline::FlatReadFloat32(r3);
    goto loc_802406B8;
}

loc_802406A8:
{
    r0 = MemoryInline::FlatRead32(r3);
    f1.d = f29.d;
    r3 = (r0 + r29);
    ctx->lr = 0x802406B8u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[8] = r8;
    ctx->gpr[11] = r11;
    ctx->gpr[27] = r27;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->fpr[3] = f3;
    ctx->fpr[4] = f4;
    ctx->fpr[5] = f5;
    ctx->fpr[6] = f6;
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
    r6 = ctx->gpr[6];
    r7 = ctx->gpr[7];
    r8 = ctx->gpr[8];
    r11 = ctx->gpr[11];
    r27 = ctx->gpr[27];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    f0 = ctx->fpr[0];
    f1 = ctx->fpr[1];
    f2 = ctx->fpr[2];
    f3 = ctx->fpr[3];
    f4 = ctx->fpr[4];
    f5 = ctx->fpr[5];
    f6 = ctx->fpr[6];
    f29 = ctx->fpr[29];
    f30 = ctx->fpr[30];
    f31 = ctx->fpr[31];
    cr = ctx->cr;
    xer = ctx->xer;
}

loc_802406B8:
{
    r0 = (r31 & 262144);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_802406BC:
{
    f30.d = f1.d;
    r3 = r30;
    r30 = (r30 + 4);
    if (((cr & 0x20000000u) != 0)) {
        goto loc_802406D4;
    }
}

loc_802406CC:
{
    f1.d = MemoryInline::FlatReadFloat32(r3);
    goto loc_802406E4;
}

loc_802406D4:
{
    r0 = MemoryInline::FlatRead32(r3);
    f1.d = f29.d;
    r3 = (r0 + r29);
    ctx->lr = 0x802406E4u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[8] = r8;
    ctx->gpr[11] = r11;
    ctx->gpr[27] = r27;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->fpr[3] = f3;
    ctx->fpr[4] = f4;
    ctx->fpr[5] = f5;
    ctx->fpr[6] = f6;
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
    r6 = ctx->gpr[6];
    r7 = ctx->gpr[7];
    r8 = ctx->gpr[8];
    r11 = ctx->gpr[11];
    r27 = ctx->gpr[27];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    f0 = ctx->fpr[0];
    f1 = ctx->fpr[1];
    f2 = ctx->fpr[2];
    f3 = ctx->fpr[3];
    f4 = ctx->fpr[4];
    f5 = ctx->fpr[5];
    f6 = ctx->fpr[6];
    f29 = ctx->fpr[29];
    f30 = ctx->fpr[30];
    f31 = ctx->fpr[31];
    cr = ctx->cr;
    xer = ctx->xer;
}

loc_802406E4:
{
    f29.d = f1.d;
    goto loc_80240FBC;
}

loc_802406EC:
{
    r0 = (r31 & 65536);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_802406F0:
{
    r3 = r30;
    r30 = (r7 + 4);
    if (((cr & 0x20000000u) != 0)) {
        goto loc_80240704;
    }
}

loc_802406FC:
{
    f1.d = MemoryInline::FlatReadFloat32(r3);
    goto loc_80240710;
}

loc_80240704:
{
    r0 = MemoryInline::FlatRead32(r3);
    r3 = (r0 + r6);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->fpr[3] = f3;
    ctx->fpr[4] = f4;
    ctx->fpr[5] = f5;
    ctx->fpr[6] = f6;
    ctx->cr = cr;
    MKW_STATIC_TRANSLATED_CALL(0x80240240u, func_80240240, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    f0 = ctx->fpr[0];
    f1 = ctx->fpr[1];
    f2 = ctx->fpr[2];
    f3 = ctx->fpr[3];
    f4 = ctx->fpr[4];
    f5 = ctx->fpr[5];
    f6 = ctx->fpr[6];
    cr = ctx->cr;
}

loc_80240710:
{
    r0 = (r31 & 131072);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_80240714:
{
    f31.d = f1.d;
    r3 = r30;
    r30 = (r30 + 4);
    if (((cr & 0x20000000u) != 0)) {
        goto loc_8024072C;
    }
}

loc_80240724:
{
    f1.d = MemoryInline::FlatReadFloat32(r3);
    goto loc_8024073C;
}

loc_8024072C:
{
    r0 = MemoryInline::FlatRead32(r3);
    f1.d = f29.d;
    r3 = (r0 + r29);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->fpr[3] = f3;
    ctx->fpr[4] = f4;
    ctx->fpr[5] = f5;
    ctx->fpr[6] = f6;
    ctx->cr = cr;
    MKW_STATIC_TRANSLATED_CALL(0x80240240u, func_80240240, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    f0 = ctx->fpr[0];
    f1 = ctx->fpr[1];
    f2 = ctx->fpr[2];
    f3 = ctx->fpr[3];
    f4 = ctx->fpr[4];
    f5 = ctx->fpr[5];
    f6 = ctx->fpr[6];
    cr = ctx->cr;
}

loc_8024073C:
{
    r0 = (r31 & 262144);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_80240740:
{
    f30.d = f1.d;
    r3 = r30;
    r30 = (r30 + 4);
    if (((cr & 0x20000000u) != 0)) {
        goto loc_80240758;
    }
}

loc_80240750:
{
    f1.d = MemoryInline::FlatReadFloat32(r3);
    goto loc_80240768;
}

loc_80240758:
{
    r0 = MemoryInline::FlatRead32(r3);
    f1.d = f29.d;
    r3 = (r0 + r29);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->fpr[3] = f3;
    ctx->fpr[4] = f4;
    ctx->fpr[5] = f5;
    ctx->fpr[6] = f6;
    ctx->cr = cr;
    MKW_STATIC_TRANSLATED_CALL(0x80240240u, func_80240240, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    f0 = ctx->fpr[0];
    f1 = ctx->fpr[1];
    f2 = ctx->fpr[2];
    f3 = ctx->fpr[3];
    f4 = ctx->fpr[4];
    f5 = ctx->fpr[5];
    f6 = ctx->fpr[6];
    cr = ctx->cr;
}

loc_80240768:
{
    f29.d = f1.d;
    goto loc_80240FBC;
}

loc_80240770:
{
    r0 = (r31 & 65536);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_80240774:
{
    r3 = r30;
    r30 = (r7 + 4);
    if (((cr & 0x20000000u) != 0)) {
        goto loc_80240788;
    }
}

loc_80240780:
{
    f1.d = MemoryInline::FlatReadFloat32(r3);
    goto loc_80240794;
}

loc_80240788:
{
    r0 = MemoryInline::FlatRead32(r3);
    r3 = (r0 + r6);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->fpr[3] = f3;
    ctx->fpr[4] = f4;
    ctx->fpr[5] = f5;
    ctx->fpr[6] = f6;
    ctx->cr = cr;
    MKW_STATIC_TRANSLATED_CALL(0x80240100u, func_80240100, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    f0 = ctx->fpr[0];
    f1 = ctx->fpr[1];
    f2 = ctx->fpr[2];
    f3 = ctx->fpr[3];
    f4 = ctx->fpr[4];
    f5 = ctx->fpr[5];
    f6 = ctx->fpr[6];
    cr = ctx->cr;
}

loc_80240794:
{
    r0 = (r31 & 131072);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_80240798:
{
    f31.d = f1.d;
    r3 = r30;
    r30 = (r30 + 4);
    if (((cr & 0x20000000u) != 0)) {
        goto loc_802407B0;
    }
}

loc_802407A8:
{
    f1.d = MemoryInline::FlatReadFloat32(r3);
    goto loc_802407C0;
}

loc_802407B0:
{
    r0 = MemoryInline::FlatRead32(r3);
    f1.d = f29.d;
    r3 = (r0 + r29);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->fpr[3] = f3;
    ctx->fpr[4] = f4;
    ctx->fpr[5] = f5;
    ctx->fpr[6] = f6;
    ctx->cr = cr;
    MKW_STATIC_TRANSLATED_CALL(0x80240100u, func_80240100, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    f0 = ctx->fpr[0];
    f1 = ctx->fpr[1];
    f2 = ctx->fpr[2];
    f3 = ctx->fpr[3];
    f4 = ctx->fpr[4];
    f5 = ctx->fpr[5];
    f6 = ctx->fpr[6];
    cr = ctx->cr;
}

loc_802407C0:
{
    r0 = (r31 & 262144);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_802407C4:
{
    f30.d = f1.d;
    r3 = r30;
    r30 = (r30 + 4);
    if (((cr & 0x20000000u) != 0)) {
        goto loc_802407DC;
    }
}

loc_802407D4:
{
    f1.d = MemoryInline::FlatReadFloat32(r3);
    goto loc_802407EC;
}

loc_802407DC:
{
    r0 = MemoryInline::FlatRead32(r3);
    f1.d = f29.d;
    r3 = (r0 + r29);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->fpr[3] = f3;
    ctx->fpr[4] = f4;
    ctx->fpr[5] = f5;
    ctx->fpr[6] = f6;
    ctx->cr = cr;
    MKW_STATIC_TRANSLATED_CALL(0x80240100u, func_80240100, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    f0 = ctx->fpr[0];
    f1 = ctx->fpr[1];
    f2 = ctx->fpr[2];
    f3 = ctx->fpr[3];
    f4 = ctx->fpr[4];
    f5 = ctx->fpr[5];
    f6 = ctx->fpr[6];
    cr = ctx->cr;
}

loc_802407EC:
{
    f29.d = f1.d;
    goto loc_80240FBC;
}

loc_802407F4:
{
    r0 = (r31 & 65536);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_802407F8:
{
    r3 = r30;
    r0 = MemoryInline::FlatRead16(r5);
    r30 = (r7 + 4);
    if (((cr & 0x20000000u) != 0)) {
        goto loc_80240810;
    }
}

loc_80240808:
{
    f31.d = MemoryInline::FlatReadFloat32(r3);
    goto loc_802408F0;
}

loc_80240810:
{
    r3 = MemoryInline::FlatRead32(r3);
    r5 = (r1 + 40);
    r4 = (r3 + r6);
    // psq_store w=1 quant=3 (using PPC_PsqSt)
    PPC_PsqStStackInline<1u, 3u>(ctx, r5, PPC_PsFromScalarInline(f29.d));
    f6.d = MemoryInline::FlatReadFloat32((r2 + -19176));
    r5 = MemoryInline::FlatRead16((r1 + 40));
    SetCRFloatResident(cr, 0, f29.d, f6.d);
}

loc_8024082C:
{
    cr = PpcCrLogicalResident(cr, static_cast<uint32_t>(7), static_cast<uint32_t>(2), static_cast<uint32_t>(0), static_cast<uint32_t>(2));
    if (((cr & 0x20000000u) == 0)) {
        goto loc_80240858;
    }
}

loc_80240834:
{
    r3 = MemoryInline::FlatRead8((r4 + 8));
    MemoryInline::FlatWriteRam32((r1 + 52), r3);
    f3.d = MemoryInline::FlatReadFloat64((r2 + -19152));
    f0.d = MemoryInline::FlatReadFloat64((r1 + 48));
    f2.d = MemoryInline::FlatReadFloat32(r4);
    f3.d = static_cast<double>(PpcForceSingleValueInline(f0.d - f3.d));
    f0.d = MemoryInline::FlatReadFloat32((r4 + 4));
    f31.d = PPC_Fmadds(f2.d, f3.d, f0.d);
    goto loc_802408F0;
}

loc_80240858:
{
    SetCRResident(cr, xer, 0, static_cast<uint32_t>(r0), static_cast<uint32_t>(r5));
}

loc_8024085C:
{
    if (((cr & 0x40000000u) != 0)) {
        goto loc_80240888;
    }
}

loc_80240860:
{
    r3 = (r4 + r0);
    f4.d = MemoryInline::FlatReadFloat64((r2 + -19152));
    r3 = MemoryInline::FlatRead8((r3 + 8));
    MemoryInline::FlatWriteRam32((r1 + 60), r3);
    f2.d = MemoryInline::FlatReadFloat32(r4);
    f3.d = MemoryInline::FlatReadFloat64((r1 + 56));
    f0.d = MemoryInline::FlatReadFloat32((r4 + 4));
    f3.d = static_cast<double>(PpcForceSingleValueInline(f3.d - f4.d));
    f31.d = PPC_Fmadds(f2.d, f3.d, f0.d);
    goto loc_802408F0;
}

loc_80240888:
{
    MemoryInline::FlatWriteRam32((r1 + 52), r5);
    r3 = (r4 + r5);
    r3 = MemoryInline::FlatRead8((r3 + 8));
    f5.d = MemoryInline::FlatReadFloat64((r2 + -19152));
    f0.d = MemoryInline::FlatReadFloat64((r1 + 48));
    MemoryInline::FlatWriteRam32((r1 + 60), r3);
    f4.d = static_cast<double>(PpcForceSingleValueInline(f0.d - f5.d));
    f3.d = MemoryInline::FlatReadFloat32(r4);
    f0.d = MemoryInline::FlatReadFloat64((r1 + 56));
    f2.d = MemoryInline::FlatReadFloat32((r4 + 4));
    f4.d = static_cast<double>(PpcForceSingleValueInline(f29.d - f4.d));
    f0.d = static_cast<double>(PpcForceSingleValueInline(f0.d - f5.d));
    SetCRFloatResident(cr, 0, f6.d, f4.d);
}

loc_802408BC:
{
    f31.d = PPC_Fmadds(f3.d, f0.d, f2.d);
    if (((cr & 0x20000000u) == 0)) {
        goto loc_802408C8;
    }
}

loc_802408C4:
{
    goto loc_802408F0;
}

loc_802408C8:
{
    r3 = (r5 + 1);
    r3 = (r3 & 65535);
    r3 = (r4 + r3);
    r3 = MemoryInline::FlatRead8((r3 + 8));
    MemoryInline::FlatWriteRam32((r1 + 52), r3);
    f0.d = MemoryInline::FlatReadFloat64((r1 + 48));
    f0.d = static_cast<double>(PpcForceSingleValueInline(f0.d - f5.d));
    f0.d = PPC_Fmadds(f3.d, f0.d, f2.d);
    f0.d = static_cast<double>(PpcForceSingleValueInline(f0.d - f31.d));
    f31.d = PPC_Fmadds(f4.d, f0.d, f31.d);
}

loc_802408F0:
{
    r3 = (r31 & 131072);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(0));
}

loc_802408F4:
{
    r3 = r30;
    r30 = (r30 + 4);
    if (((cr & 0x20000000u) != 0)) {
        goto loc_80240908;
    }
}

loc_80240900:
{
    f30.d = MemoryInline::FlatReadFloat32(r3);
    goto loc_802409E8;
}

loc_80240908:
{
    r3 = MemoryInline::FlatRead32(r3);
    r5 = (r1 + 36);
    r4 = (r3 + r6);
    // psq_store w=1 quant=3 (using PPC_PsqSt)
    PPC_PsqStStackInline<1u, 3u>(ctx, r5, PPC_PsFromScalarInline(f1.d));
    f6.d = MemoryInline::FlatReadFloat32((r2 + -19176));
    r5 = MemoryInline::FlatRead16((r1 + 36));
    SetCRFloatResident(cr, 0, f1.d, f6.d);
}

loc_80240924:
{
    cr = PpcCrLogicalResident(cr, static_cast<uint32_t>(7), static_cast<uint32_t>(2), static_cast<uint32_t>(0), static_cast<uint32_t>(2));
    if (((cr & 0x20000000u) == 0)) {
        goto loc_80240950;
    }
}

loc_8024092C:
{
    r3 = MemoryInline::FlatRead8((r4 + 8));
    MemoryInline::FlatWriteRam32((r1 + 60), r3);
    f3.d = MemoryInline::FlatReadFloat64((r2 + -19152));
    f0.d = MemoryInline::FlatReadFloat64((r1 + 56));
    f2.d = MemoryInline::FlatReadFloat32(r4);
    f3.d = static_cast<double>(PpcForceSingleValueInline(f0.d - f3.d));
    f0.d = MemoryInline::FlatReadFloat32((r4 + 4));
    f30.d = PPC_Fmadds(f2.d, f3.d, f0.d);
    goto loc_802409E8;
}

loc_80240950:
{
    SetCRResident(cr, xer, 0, static_cast<uint32_t>(r0), static_cast<uint32_t>(r5));
}

loc_80240954:
{
    if (((cr & 0x40000000u) != 0)) {
        goto loc_80240980;
    }
}

loc_80240958:
{
    r3 = (r4 + r0);
    f4.d = MemoryInline::FlatReadFloat64((r2 + -19152));
    r3 = MemoryInline::FlatRead8((r3 + 8));
    MemoryInline::FlatWriteRam32((r1 + 52), r3);
    f2.d = MemoryInline::FlatReadFloat32(r4);
    f3.d = MemoryInline::FlatReadFloat64((r1 + 48));
    f0.d = MemoryInline::FlatReadFloat32((r4 + 4));
    f3.d = static_cast<double>(PpcForceSingleValueInline(f3.d - f4.d));
    f30.d = PPC_Fmadds(f2.d, f3.d, f0.d);
    goto loc_802409E8;
}

loc_80240980:
{
    MemoryInline::FlatWriteRam32((r1 + 60), r5);
    r3 = (r4 + r5);
    r3 = MemoryInline::FlatRead8((r3 + 8));
    f5.d = MemoryInline::FlatReadFloat64((r2 + -19152));
    f0.d = MemoryInline::FlatReadFloat64((r1 + 56));
    MemoryInline::FlatWriteRam32((r1 + 52), r3);
    f4.d = static_cast<double>(PpcForceSingleValueInline(f0.d - f5.d));
    f3.d = MemoryInline::FlatReadFloat32(r4);
    f0.d = MemoryInline::FlatReadFloat64((r1 + 48));
    f2.d = MemoryInline::FlatReadFloat32((r4 + 4));
    f4.d = static_cast<double>(PpcForceSingleValueInline(f1.d - f4.d));
    f0.d = static_cast<double>(PpcForceSingleValueInline(f0.d - f5.d));
    SetCRFloatResident(cr, 0, f6.d, f4.d);
}

loc_802409B4:
{
    f30.d = PPC_Fmadds(f3.d, f0.d, f2.d);
    if (((cr & 0x20000000u) == 0)) {
        goto loc_802409C0;
    }
}

loc_802409BC:
{
    goto loc_802409E8;
}

loc_802409C0:
{
    r3 = (r5 + 1);
    r3 = (r3 & 65535);
    r3 = (r4 + r3);
    r3 = MemoryInline::FlatRead8((r3 + 8));
    MemoryInline::FlatWriteRam32((r1 + 60), r3);
    f0.d = MemoryInline::FlatReadFloat64((r1 + 56));
    f0.d = static_cast<double>(PpcForceSingleValueInline(f0.d - f5.d));
    f0.d = PPC_Fmadds(f3.d, f0.d, f2.d);
    f0.d = static_cast<double>(PpcForceSingleValueInline(f0.d - f30.d));
    f30.d = PPC_Fmadds(f4.d, f0.d, f30.d);
}

loc_802409E8:
{
    r3 = (r31 & 262144);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(0));
}

loc_802409EC:
{
    r3 = r30;
    r30 = (r30 + 4);
    if (((cr & 0x20000000u) != 0)) {
        goto loc_80240A00;
    }
}

loc_802409F8:
{
    f29.d = MemoryInline::FlatReadFloat32(r3);
    goto loc_80240FBC;
}

loc_80240A00:
{
    r3 = MemoryInline::FlatRead32(r3);
    r5 = (r1 + 32);
    r4 = (r3 + r6);
    // psq_store w=1 quant=3 (using PPC_PsqSt)
    PPC_PsqStStackInline<1u, 3u>(ctx, r5, PPC_PsFromScalarInline(f1.d));
    f6.d = MemoryInline::FlatReadFloat32((r2 + -19176));
    r5 = MemoryInline::FlatRead16((r1 + 32));
    SetCRFloatResident(cr, 0, f1.d, f6.d);
}

loc_80240A1C:
{
    cr = PpcCrLogicalResident(cr, static_cast<uint32_t>(7), static_cast<uint32_t>(2), static_cast<uint32_t>(0), static_cast<uint32_t>(2));
    if (((cr & 0x20000000u) == 0)) {
        goto loc_80240A48;
    }
}

loc_80240A24:
{
    r0 = MemoryInline::FlatRead8((r4 + 8));
    MemoryInline::FlatWriteRam32((r1 + 52), r0);
    f2.d = MemoryInline::FlatReadFloat64((r2 + -19152));
    f0.d = MemoryInline::FlatReadFloat64((r1 + 48));
    f1.d = MemoryInline::FlatReadFloat32(r4);
    f2.d = static_cast<double>(PpcForceSingleValueInline(f0.d - f2.d));
    f0.d = MemoryInline::FlatReadFloat32((r4 + 4));
    f29.d = PPC_Fmadds(f1.d, f2.d, f0.d);
    goto loc_80240FBC;
}

loc_80240A48:
{
    SetCRResident(cr, xer, 0, static_cast<uint32_t>(r0), static_cast<uint32_t>(r5));
}

loc_80240A4C:
{
    if (((cr & 0x40000000u) != 0)) {
        goto loc_80240A78;
    }
}

loc_80240A50:
{
    r3 = (r4 + r0);
    f3.d = MemoryInline::FlatReadFloat64((r2 + -19152));
    r0 = MemoryInline::FlatRead8((r3 + 8));
    MemoryInline::FlatWriteRam32((r1 + 60), r0);
    f1.d = MemoryInline::FlatReadFloat32(r4);
    f2.d = MemoryInline::FlatReadFloat64((r1 + 56));
    f0.d = MemoryInline::FlatReadFloat32((r4 + 4));
    f2.d = static_cast<double>(PpcForceSingleValueInline(f2.d - f3.d));
    f29.d = PPC_Fmadds(f1.d, f2.d, f0.d);
    goto loc_80240FBC;
}

loc_80240A78:
{
    MemoryInline::FlatWriteRam32((r1 + 52), r5);
    r3 = (r4 + r5);
    r0 = MemoryInline::FlatRead8((r3 + 8));
    f5.d = MemoryInline::FlatReadFloat64((r2 + -19152));
    f0.d = MemoryInline::FlatReadFloat64((r1 + 48));
    MemoryInline::FlatWriteRam32((r1 + 60), r0);
    f4.d = static_cast<double>(PpcForceSingleValueInline(f0.d - f5.d));
    f3.d = MemoryInline::FlatReadFloat32(r4);
    f0.d = MemoryInline::FlatReadFloat64((r1 + 56));
    f2.d = MemoryInline::FlatReadFloat32((r4 + 4));
    f1.d = static_cast<double>(PpcForceSingleValueInline(f1.d - f4.d));
    f0.d = static_cast<double>(PpcForceSingleValueInline(f0.d - f5.d));
    SetCRFloatResident(cr, 0, f6.d, f1.d);
}

loc_80240AAC:
{
    f29.d = PPC_Fmadds(f3.d, f0.d, f2.d);
    if (((cr & 0x20000000u) == 0)) {
        goto loc_80240AB8;
    }
}

loc_80240AB4:
{
    goto loc_80240FBC;
}

loc_80240AB8:
{
    r0 = (r5 + 1);
    r0 = (r0 & 65535);
    r3 = (r4 + r0);
    r0 = MemoryInline::FlatRead8((r3 + 8));
    MemoryInline::FlatWriteRam32((r1 + 52), r0);
    f0.d = MemoryInline::FlatReadFloat64((r1 + 48));
    f0.d = static_cast<double>(PpcForceSingleValueInline(f0.d - f5.d));
    f0.d = PPC_Fmadds(f3.d, f0.d, f2.d);
    f0.d = static_cast<double>(PpcForceSingleValueInline(f0.d - f29.d));
    f29.d = PPC_Fmadds(f1.d, f0.d, f29.d);
    goto loc_80240FBC;
}

loc_80240AE4:
{
    r0 = (r31 & 65536);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_80240AE8:
{
    r3 = r30;
    r0 = MemoryInline::FlatRead16(r5);
    r30 = (r7 + 4);
    if (((cr & 0x20000000u) != 0)) {
        goto loc_80240B00;
    }
}

loc_80240AF8:
{
    f31.d = MemoryInline::FlatReadFloat32(r3);
    goto loc_80240BE8;
}

loc_80240B00:
{
    r3 = MemoryInline::FlatRead32(r3);
    r5 = (r1 + 28);
    r4 = (r3 + r6);
    // psq_store w=1 quant=3 (using PPC_PsqSt)
    PPC_PsqStStackInline<1u, 3u>(ctx, r5, PPC_PsFromScalarInline(f29.d));
    f5.d = MemoryInline::FlatReadFloat32((r2 + -19176));
    r5 = MemoryInline::FlatRead16((r1 + 28));
    SetCRFloatResident(cr, 0, f29.d, f5.d);
}

loc_80240B1C:
{
    cr = PpcCrLogicalResident(cr, static_cast<uint32_t>(7), static_cast<uint32_t>(2), static_cast<uint32_t>(0), static_cast<uint32_t>(2));
    if (((cr & 0x20000000u) == 0)) {
        goto loc_80240B48;
    }
}

loc_80240B24:
{
    r3 = MemoryInline::FlatRead16((r4 + 8));
    MemoryInline::FlatWriteRam32((r1 + 60), r3);
    f3.d = MemoryInline::FlatReadFloat64((r2 + -19152));
    f0.d = MemoryInline::FlatReadFloat64((r1 + 56));
    f2.d = MemoryInline::FlatReadFloat32(r4);
    f3.d = static_cast<double>(PpcForceSingleValueInline(f0.d - f3.d));
    f0.d = MemoryInline::FlatReadFloat32((r4 + 4));
    f31.d = PPC_Fmadds(f2.d, f3.d, f0.d);
    goto loc_80240BE8;
}

loc_80240B48:
{
    SetCRResident(cr, xer, 0, static_cast<uint32_t>(r0), static_cast<uint32_t>(r5));
}

loc_80240B4C:
{
    if (((cr & 0x40000000u) != 0)) {
        goto loc_80240B7C;
    }
}

loc_80240B50:
{
    r3_rot_9 = PpcRotl32Inline(static_cast<uint32_t>(r0), static_cast<uint32_t>(1));
    r3 = (r3_rot_9 & 131070);
    f4.d = MemoryInline::FlatReadFloat64((r2 + -19152));
    r3 = (r4 + r3);
    f2.d = MemoryInline::FlatReadFloat32(r4);
    r3 = MemoryInline::FlatRead16((r3 + 8));
    MemoryInline::FlatWriteRam32((r1 + 52), r3);
    f0.d = MemoryInline::FlatReadFloat32((r4 + 4));
    f3.d = MemoryInline::FlatReadFloat64((r1 + 48));
    f3.d = static_cast<double>(PpcForceSingleValueInline(f3.d - f4.d));
    f31.d = PPC_Fmadds(f2.d, f3.d, f0.d);
    goto loc_80240BE8;
}

loc_80240B7C:
{
    MemoryInline::FlatWriteRam32((r1 + 60), r5);
    r3_rot_10 = PpcRotl32Inline(static_cast<uint32_t>(r5), static_cast<uint32_t>(1));
    r3 = (r3_rot_10 & 131070);
    r3 = (r4 + r3);
    f4.d = MemoryInline::FlatReadFloat64((r2 + -19152));
    f0.d = MemoryInline::FlatReadFloat64((r1 + 56));
    r3 = MemoryInline::FlatRead16((r3 + 8));
    f2.d = static_cast<double>(PpcForceSingleValueInline(f0.d - f4.d));
    MemoryInline::FlatWriteRam32((r1 + 52), r3);
    f3.d = MemoryInline::FlatReadFloat32(r4);
    f0.d = MemoryInline::FlatReadFloat64((r1 + 48));
    f6.d = static_cast<double>(PpcForceSingleValueInline(f29.d - f2.d));
    f2.d = MemoryInline::FlatReadFloat32((r4 + 4));
    f0.d = static_cast<double>(PpcForceSingleValueInline(f0.d - f4.d));
    SetCRFloatResident(cr, 0, f5.d, f6.d);
}

loc_80240BB4:
{
    f31.d = PPC_Fmadds(f3.d, f0.d, f2.d);
    if (((cr & 0x20000000u) == 0)) {
        goto loc_80240BC0;
    }
}

loc_80240BBC:
{
    goto loc_80240BE8;
}

loc_80240BC0:
{
    r3 = (r5 + 1);
    r3_rot_11 = PpcRotl32Inline(static_cast<uint32_t>(r3), static_cast<uint32_t>(1));
    r3 = (r3_rot_11 & 131070);
    r3 = (r4 + r3);
    r3 = MemoryInline::FlatRead16((r3 + 8));
    MemoryInline::FlatWriteRam32((r1 + 60), r3);
    f0.d = MemoryInline::FlatReadFloat64((r1 + 56));
    f0.d = static_cast<double>(PpcForceSingleValueInline(f0.d - f4.d));
    f0.d = PPC_Fmadds(f3.d, f0.d, f2.d);
    f0.d = static_cast<double>(PpcForceSingleValueInline(f0.d - f31.d));
    f31.d = PPC_Fmadds(f6.d, f0.d, f31.d);
}

loc_80240BE8:
{
    r3 = (r31 & 131072);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(0));
}

loc_80240BEC:
{
    r3 = r30;
    r30 = (r30 + 4);
    if (((cr & 0x20000000u) != 0)) {
        goto loc_80240C00;
    }
}

loc_80240BF8:
{
    f30.d = MemoryInline::FlatReadFloat32(r3);
    goto loc_80240CE8;
}

loc_80240C00:
{
    r3 = MemoryInline::FlatRead32(r3);
    r5 = (r1 + 24);
    r4 = (r3 + r6);
    // psq_store w=1 quant=3 (using PPC_PsqSt)
    PPC_PsqStStackInline<1u, 3u>(ctx, r5, PPC_PsFromScalarInline(f1.d));
    f5.d = MemoryInline::FlatReadFloat32((r2 + -19176));
    r5 = MemoryInline::FlatRead16((r1 + 24));
    SetCRFloatResident(cr, 0, f1.d, f5.d);
}

loc_80240C1C:
{
    cr = PpcCrLogicalResident(cr, static_cast<uint32_t>(7), static_cast<uint32_t>(2), static_cast<uint32_t>(0), static_cast<uint32_t>(2));
    if (((cr & 0x20000000u) == 0)) {
        goto loc_80240C48;
    }
}

loc_80240C24:
{
    r3 = MemoryInline::FlatRead16((r4 + 8));
    MemoryInline::FlatWriteRam32((r1 + 52), r3);
    f3.d = MemoryInline::FlatReadFloat64((r2 + -19152));
    f0.d = MemoryInline::FlatReadFloat64((r1 + 48));
    f2.d = MemoryInline::FlatReadFloat32(r4);
    f3.d = static_cast<double>(PpcForceSingleValueInline(f0.d - f3.d));
    f0.d = MemoryInline::FlatReadFloat32((r4 + 4));
    f30.d = PPC_Fmadds(f2.d, f3.d, f0.d);
    goto loc_80240CE8;
}

loc_80240C48:
{
    SetCRResident(cr, xer, 0, static_cast<uint32_t>(r0), static_cast<uint32_t>(r5));
}

loc_80240C4C:
{
    if (((cr & 0x40000000u) != 0)) {
        goto loc_80240C7C;
    }
}

loc_80240C50:
{
    r3_rot_13 = PpcRotl32Inline(static_cast<uint32_t>(r0), static_cast<uint32_t>(1));
    r3 = (r3_rot_13 & 131070);
    f4.d = MemoryInline::FlatReadFloat64((r2 + -19152));
    r3 = (r4 + r3);
    f2.d = MemoryInline::FlatReadFloat32(r4);
    r3 = MemoryInline::FlatRead16((r3 + 8));
    MemoryInline::FlatWriteRam32((r1 + 60), r3);
    f0.d = MemoryInline::FlatReadFloat32((r4 + 4));
    f3.d = MemoryInline::FlatReadFloat64((r1 + 56));
    f3.d = static_cast<double>(PpcForceSingleValueInline(f3.d - f4.d));
    f30.d = PPC_Fmadds(f2.d, f3.d, f0.d);
    goto loc_80240CE8;
}

loc_80240C7C:
{
    MemoryInline::FlatWriteRam32((r1 + 52), r5);
    r3_rot_14 = PpcRotl32Inline(static_cast<uint32_t>(r5), static_cast<uint32_t>(1));
    r3 = (r3_rot_14 & 131070);
    r3 = (r4 + r3);
    f4.d = MemoryInline::FlatReadFloat64((r2 + -19152));
    f0.d = MemoryInline::FlatReadFloat64((r1 + 48));
    r3 = MemoryInline::FlatRead16((r3 + 8));
    f2.d = static_cast<double>(PpcForceSingleValueInline(f0.d - f4.d));
    MemoryInline::FlatWriteRam32((r1 + 60), r3);
    f3.d = MemoryInline::FlatReadFloat32(r4);
    f0.d = MemoryInline::FlatReadFloat64((r1 + 56));
    f6.d = static_cast<double>(PpcForceSingleValueInline(f1.d - f2.d));
    f2.d = MemoryInline::FlatReadFloat32((r4 + 4));
    f0.d = static_cast<double>(PpcForceSingleValueInline(f0.d - f4.d));
    SetCRFloatResident(cr, 0, f5.d, f6.d);
}

loc_80240CB4:
{
    f30.d = PPC_Fmadds(f3.d, f0.d, f2.d);
    if (((cr & 0x20000000u) == 0)) {
        goto loc_80240CC0;
    }
}

loc_80240CBC:
{
    goto loc_80240CE8;
}

loc_80240CC0:
{
    r3 = (r5 + 1);
    r3_rot_15 = PpcRotl32Inline(static_cast<uint32_t>(r3), static_cast<uint32_t>(1));
    r3 = (r3_rot_15 & 131070);
    r3 = (r4 + r3);
    r3 = MemoryInline::FlatRead16((r3 + 8));
    MemoryInline::FlatWriteRam32((r1 + 52), r3);
    f0.d = MemoryInline::FlatReadFloat64((r1 + 48));
    f0.d = static_cast<double>(PpcForceSingleValueInline(f0.d - f4.d));
    f0.d = PPC_Fmadds(f3.d, f0.d, f2.d);
    f0.d = static_cast<double>(PpcForceSingleValueInline(f0.d - f30.d));
    f30.d = PPC_Fmadds(f6.d, f0.d, f30.d);
}

loc_80240CE8:
{
    r3 = (r31 & 262144);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(0));
}

loc_80240CEC:
{
    r3 = r30;
    r30 = (r30 + 4);
    if (((cr & 0x20000000u) != 0)) {
        goto loc_80240D00;
    }
}

loc_80240CF8:
{
    f29.d = MemoryInline::FlatReadFloat32(r3);
    goto loc_80240FBC;
}

loc_80240D00:
{
    r3 = MemoryInline::FlatRead32(r3);
    r5 = (r1 + 20);
    r4 = (r3 + r6);
    // psq_store w=1 quant=3 (using PPC_PsqSt)
    PPC_PsqStStackInline<1u, 3u>(ctx, r5, PPC_PsFromScalarInline(f1.d));
    f5.d = MemoryInline::FlatReadFloat32((r2 + -19176));
    r5 = MemoryInline::FlatRead16((r1 + 20));
    SetCRFloatResident(cr, 0, f1.d, f5.d);
}

loc_80240D1C:
{
    cr = PpcCrLogicalResident(cr, static_cast<uint32_t>(7), static_cast<uint32_t>(2), static_cast<uint32_t>(0), static_cast<uint32_t>(2));
    if (((cr & 0x20000000u) == 0)) {
        goto loc_80240D48;
    }
}

loc_80240D24:
{
    r0 = MemoryInline::FlatRead16((r4 + 8));
    MemoryInline::FlatWriteRam32((r1 + 60), r0);
    f2.d = MemoryInline::FlatReadFloat64((r2 + -19152));
    f0.d = MemoryInline::FlatReadFloat64((r1 + 56));
    f1.d = MemoryInline::FlatReadFloat32(r4);
    f2.d = static_cast<double>(PpcForceSingleValueInline(f0.d - f2.d));
    f0.d = MemoryInline::FlatReadFloat32((r4 + 4));
    f29.d = PPC_Fmadds(f1.d, f2.d, f0.d);
    goto loc_80240FBC;
}

loc_80240D48:
{
    SetCRResident(cr, xer, 0, static_cast<uint32_t>(r0), static_cast<uint32_t>(r5));
}

loc_80240D4C:
{
    if (((cr & 0x40000000u) != 0)) {
        goto loc_80240D7C;
    }
}

loc_80240D50:
{
    r0_rot_4 = PpcRotl32Inline(static_cast<uint32_t>(r0), static_cast<uint32_t>(1));
    r0 = (r0_rot_4 & 131070);
    f3.d = MemoryInline::FlatReadFloat64((r2 + -19152));
    r3 = (r4 + r0);
    f1.d = MemoryInline::FlatReadFloat32(r4);
    r0 = MemoryInline::FlatRead16((r3 + 8));
    MemoryInline::FlatWriteRam32((r1 + 52), r0);
    f0.d = MemoryInline::FlatReadFloat32((r4 + 4));
    f2.d = MemoryInline::FlatReadFloat64((r1 + 48));
    f2.d = static_cast<double>(PpcForceSingleValueInline(f2.d - f3.d));
    f29.d = PPC_Fmadds(f1.d, f2.d, f0.d);
    goto loc_80240FBC;
}

loc_80240D7C:
{
    MemoryInline::FlatWriteRam32((r1 + 60), r5);
    r0_rot_5 = PpcRotl32Inline(static_cast<uint32_t>(r5), static_cast<uint32_t>(1));
    r0 = (r0_rot_5 & 131070);
    r3 = (r4 + r0);
    f4.d = MemoryInline::FlatReadFloat64((r2 + -19152));
    f0.d = MemoryInline::FlatReadFloat64((r1 + 56));
    r0 = MemoryInline::FlatRead16((r3 + 8));
    f3.d = static_cast<double>(PpcForceSingleValueInline(f0.d - f4.d));
    MemoryInline::FlatWriteRam32((r1 + 52), r0);
    f2.d = MemoryInline::FlatReadFloat32(r4);
    f0.d = MemoryInline::FlatReadFloat64((r1 + 48));
    f3.d = static_cast<double>(PpcForceSingleValueInline(f1.d - f3.d));
    f1.d = MemoryInline::FlatReadFloat32((r4 + 4));
    f0.d = static_cast<double>(PpcForceSingleValueInline(f0.d - f4.d));
    SetCRFloatResident(cr, 0, f5.d, f3.d);
}

loc_80240DB4:
{
    f29.d = PPC_Fmadds(f2.d, f0.d, f1.d);
    if (((cr & 0x20000000u) == 0)) {
        goto loc_80240DC0;
    }
}

loc_80240DBC:
{
    goto loc_80240FBC;
}

loc_80240DC0:
{
    r0 = (r5 + 1);
    r0_rot_6 = PpcRotl32Inline(static_cast<uint32_t>(r0), static_cast<uint32_t>(1));
    r0 = (r0_rot_6 & 131070);
    r3 = (r4 + r0);
    r0 = MemoryInline::FlatRead16((r3 + 8));
    MemoryInline::FlatWriteRam32((r1 + 60), r0);
    f0.d = MemoryInline::FlatReadFloat64((r1 + 56));
    f0.d = static_cast<double>(PpcForceSingleValueInline(f0.d - f4.d));
    f0.d = PPC_Fmadds(f2.d, f0.d, f1.d);
    f0.d = static_cast<double>(PpcForceSingleValueInline(f0.d - f29.d));
    f29.d = PPC_Fmadds(f3.d, f0.d, f29.d);
    goto loc_80240FBC;
}

loc_80240DEC:
{
    r0 = (r31 & 65536);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_80240DF0:
{
    r3 = r30;
    r0 = MemoryInline::FlatRead16(r5);
    r30 = (r7 + 4);
    if (((cr & 0x20000000u) != 0)) {
        goto loc_80240E08;
    }
}

loc_80240E00:
{
    f31.d = MemoryInline::FlatReadFloat32(r3);
    goto loc_80240E84;
}

loc_80240E08:
{
    r3 = MemoryInline::FlatRead32(r3);
    r5 = (r1 + 16);
    r4 = (r3 + r6);
    // psq_store w=1 quant=3 (using PPC_PsqSt)
    PPC_PsqStStackInline<1u, 3u>(ctx, r5, PPC_PsFromScalarInline(f29.d));
    f3.d = MemoryInline::FlatReadFloat32((r2 + -19176));
    r5 = MemoryInline::FlatRead16((r1 + 16));
    SetCRFloatResident(cr, 0, f29.d, f3.d);
}

loc_80240E24:
{
    cr = PpcCrLogicalResident(cr, static_cast<uint32_t>(7), static_cast<uint32_t>(2), static_cast<uint32_t>(0), static_cast<uint32_t>(2));
    if (((cr & 0x20000000u) == 0)) {
        goto loc_80240E34;
    }
}

loc_80240E2C:
{
    f31.d = MemoryInline::FlatReadFloat32(r4);
    goto loc_80240E84;
}

loc_80240E34:
{
    SetCRResident(cr, xer, 0, static_cast<uint32_t>(r0), static_cast<uint32_t>(r5));
}

loc_80240E38:
{
    if (((cr & 0x40000000u) != 0)) {
        goto loc_80240E48;
    }
}

loc_80240E3C:
{
    r3_rot_1 = PpcRotl32Inline(static_cast<uint32_t>(r0), static_cast<uint32_t>(2));
    r3 = (r3_rot_1 & 262140);
    addr_lfsx_80240E40_loc_0 = (r4 + r3);
    f31.d = MemoryInline::FlatReadFloat32(addr_lfsx_80240E40_loc_0);
    goto loc_80240E84;
}

loc_80240E48:
{
    MemoryInline::FlatWriteRam32((r1 + 52), r5);
    r3_rot_2 = PpcRotl32Inline(static_cast<uint32_t>(r5), static_cast<uint32_t>(2));
    r3 = (r3_rot_2 & 262140);
    f2.d = MemoryInline::FlatReadFloat64((r2 + -19152));
    f0.d = MemoryInline::FlatReadFloat64((r1 + 48));
    addr_lfsx_80240E58_loc_0 = (r4 + r3);
    f31.d = MemoryInline::FlatReadFloat32(addr_lfsx_80240E58_loc_0);
    f0.d = static_cast<double>(PpcForceSingleValueInline(f0.d - f2.d));
    f2.d = static_cast<double>(PpcForceSingleValueInline(f29.d - f0.d));
    SetCRFloatResident(cr, 0, f3.d, f2.d);
}

loc_80240E68:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_80240E70;
    }
}

loc_80240E6C:
{
    goto loc_80240E84;
}

loc_80240E70:
{
    r3 = (r5 + 1);
    r3_rot_3 = PpcRotl32Inline(static_cast<uint32_t>(r3), static_cast<uint32_t>(2));
    r3 = (r3_rot_3 & 262140);
    addr_lfsx_80240E78_loc_0 = (r4 + r3);
    f0.d = MemoryInline::FlatReadFloat32(addr_lfsx_80240E78_loc_0);
    f0.d = static_cast<double>(PpcForceSingleValueInline(f0.d - f31.d));
    f31.d = PPC_Fmadds(f2.d, f0.d, f31.d);
}

loc_80240E84:
{
    r3 = (r31 & 131072);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(0));
}

loc_80240E88:
{
    r3 = r30;
    r30 = (r30 + 4);
    if (((cr & 0x20000000u) != 0)) {
        goto loc_80240E9C;
    }
}

loc_80240E94:
{
    f30.d = MemoryInline::FlatReadFloat32(r3);
    goto loc_80240F18;
}

loc_80240E9C:
{
    r3 = MemoryInline::FlatRead32(r3);
    r5 = (r1 + 12);
    r4 = (r3 + r6);
    // psq_store w=1 quant=3 (using PPC_PsqSt)
    PPC_PsqStStackInline<1u, 3u>(ctx, r5, PPC_PsFromScalarInline(f1.d));
    f3.d = MemoryInline::FlatReadFloat32((r2 + -19176));
    r5 = MemoryInline::FlatRead16((r1 + 12));
    SetCRFloatResident(cr, 0, f1.d, f3.d);
}

loc_80240EB8:
{
    cr = PpcCrLogicalResident(cr, static_cast<uint32_t>(7), static_cast<uint32_t>(2), static_cast<uint32_t>(0), static_cast<uint32_t>(2));
    if (((cr & 0x20000000u) == 0)) {
        goto loc_80240EC8;
    }
}

loc_80240EC0:
{
    f30.d = MemoryInline::FlatReadFloat32(r4);
    goto loc_80240F18;
}

loc_80240EC8:
{
    SetCRResident(cr, xer, 0, static_cast<uint32_t>(r0), static_cast<uint32_t>(r5));
}

loc_80240ECC:
{
    if (((cr & 0x40000000u) != 0)) {
        goto loc_80240EDC;
    }
}

loc_80240ED0:
{
    r3_rot_5 = PpcRotl32Inline(static_cast<uint32_t>(r0), static_cast<uint32_t>(2));
    r3 = (r3_rot_5 & 262140);
    addr_lfsx_80240ED4_loc_0 = (r4 + r3);
    f30.d = MemoryInline::FlatReadFloat32(addr_lfsx_80240ED4_loc_0);
    goto loc_80240F18;
}

loc_80240EDC:
{
    MemoryInline::FlatWriteRam32((r1 + 60), r5);
    r3_rot_6 = PpcRotl32Inline(static_cast<uint32_t>(r5), static_cast<uint32_t>(2));
    r3 = (r3_rot_6 & 262140);
    f2.d = MemoryInline::FlatReadFloat64((r2 + -19152));
    f0.d = MemoryInline::FlatReadFloat64((r1 + 56));
    addr_lfsx_80240EEC_loc_0 = (r4 + r3);
    f30.d = MemoryInline::FlatReadFloat32(addr_lfsx_80240EEC_loc_0);
    f0.d = static_cast<double>(PpcForceSingleValueInline(f0.d - f2.d));
    f2.d = static_cast<double>(PpcForceSingleValueInline(f1.d - f0.d));
    SetCRFloatResident(cr, 0, f3.d, f2.d);
}

loc_80240EFC:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_80240F04;
    }
}

loc_80240F00:
{
    goto loc_80240F18;
}

loc_80240F04:
{
    r3 = (r5 + 1);
    r3_rot_7 = PpcRotl32Inline(static_cast<uint32_t>(r3), static_cast<uint32_t>(2));
    r3 = (r3_rot_7 & 262140);
    addr_lfsx_80240F0C_loc_0 = (r4 + r3);
    f0.d = MemoryInline::FlatReadFloat32(addr_lfsx_80240F0C_loc_0);
    f0.d = static_cast<double>(PpcForceSingleValueInline(f0.d - f30.d));
    f30.d = PPC_Fmadds(f2.d, f0.d, f30.d);
}

loc_80240F18:
{
    r3 = (r31 & 262144);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(0));
}

loc_80240F1C:
{
    r3 = r30;
    r30 = (r30 + 4);
    if (((cr & 0x20000000u) != 0)) {
        goto loc_80240F30;
    }
}

loc_80240F28:
{
    f29.d = MemoryInline::FlatReadFloat32(r3);
    goto loc_80240FBC;
}

loc_80240F30:
{
    r3 = MemoryInline::FlatRead32(r3);
    r4 = (r1 + 8);
    r3 = (r3 + r6);
    // psq_store w=1 quant=3 (using PPC_PsqSt)
    PPC_PsqStStackInline<1u, 3u>(ctx, r4, PPC_PsFromScalarInline(f1.d));
    f3.d = MemoryInline::FlatReadFloat32((r2 + -19176));
    r4 = MemoryInline::FlatRead16((r1 + 8));
    SetCRFloatResident(cr, 0, f1.d, f3.d);
}

loc_80240F4C:
{
    cr = PpcCrLogicalResident(cr, static_cast<uint32_t>(7), static_cast<uint32_t>(2), static_cast<uint32_t>(0), static_cast<uint32_t>(2));
    if (((cr & 0x20000000u) == 0)) {
        goto loc_80240F5C;
    }
}

loc_80240F54:
{
    f29.d = MemoryInline::FlatReadFloat32(r3);
    goto loc_80240FBC;
}

loc_80240F5C:
{
    SetCRResident(cr, xer, 0, static_cast<uint32_t>(r0), static_cast<uint32_t>(r4));
}

loc_80240F60:
{
    if (((cr & 0x40000000u) != 0)) {
        goto loc_80240F70;
    }
}

loc_80240F64:
{
    r0_rot_1 = PpcRotl32Inline(static_cast<uint32_t>(r0), static_cast<uint32_t>(2));
    r0 = (r0_rot_1 & 262140);
    addr_lfsx_80240F68_loc_0 = (r3 + r0);
    f29.d = MemoryInline::FlatReadFloat32(addr_lfsx_80240F68_loc_0);
    goto loc_80240FBC;
}

loc_80240F70:
{
    MemoryInline::FlatWriteRam32((r1 + 52), r4);
    r0_rot_2 = PpcRotl32Inline(static_cast<uint32_t>(r4), static_cast<uint32_t>(2));
    r0 = (r0_rot_2 & 262140);
    f2.d = MemoryInline::FlatReadFloat64((r2 + -19152));
    f0.d = MemoryInline::FlatReadFloat64((r1 + 48));
    addr_lfsx_80240F80_loc_0 = (r3 + r0);
    f29.d = MemoryInline::FlatReadFloat32(addr_lfsx_80240F80_loc_0);
    f0.d = static_cast<double>(PpcForceSingleValueInline(f0.d - f2.d));
    f1.d = static_cast<double>(PpcForceSingleValueInline(f1.d - f0.d));
    SetCRFloatResident(cr, 0, f3.d, f1.d);
}

loc_80240F90:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_80240F98;
    }
}

loc_80240F94:
{
    goto loc_80240FBC;
}

loc_80240F98:
{
    r0 = (r4 + 1);
    r0_rot_3 = PpcRotl32Inline(static_cast<uint32_t>(r0), static_cast<uint32_t>(2));
    r0 = (r0_rot_3 & 262140);
    addr_lfsx_80240FA0_loc_0 = (r3 + r0);
    f0.d = MemoryInline::FlatReadFloat32(addr_lfsx_80240FA0_loc_0);
    f0.d = static_cast<double>(PpcForceSingleValueInline(f0.d - f29.d));
    f29.d = PPC_Fmadds(f1.d, f0.d, f29.d);
    goto loc_80240FBC;
}

loc_80240FB0:
{
    f31.d = MemoryInline::FlatReadFloat32((r2 + -19176));
    f30.d = f31.d;
    f29.d = f31.d;
}

loc_80240FBC:
{
    f0.d = MemoryInline::FlatReadFloat32((r2 + -19160));
    r3 = r27;
    f1.d = PpcFmulsInline(f0.d, f31.d);
    f2.d = PpcFmulsInline(f0.d, f30.d);
    f3.d = PpcFmulsInline(f0.d, f29.d);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->fpr[3] = f3;
    ctx->fpr[4] = f4;
    ctx->fpr[5] = f5;
    ctx->fpr[6] = f6;
    ctx->cr = cr;
    MKW_STATIC_TRANSLATED_CALL(0x802382B0u, func_802382B0, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r7 = ctx->gpr[7];
    f0 = ctx->fpr[0];
    f1 = ctx->fpr[1];
    f2 = ctx->fpr[2];
    f3 = ctx->fpr[3];
    f4 = ctx->fpr[4];
    f5 = ctx->fpr[5];
    f6 = ctx->fpr[6];
    cr = ctx->cr;
    MemoryInline::FlatWriteFloat32(r28, f31.d);
    r3 = r30;
    MemoryInline::FlatWriteFloat32((r28 + 4), f30.d);
    MemoryInline::FlatWriteFloat32((r28 + 8), f29.d);
    // psq_load w=0 quant=0 (using PPC_PsqL)
    guest_range_1 = MemoryInline::ResolveRangeHost((r1 + 76), 0, 76u, true, false);
    f31.d = MemoryInline::ReadResolvedFloat64(guest_range_1, 52u, (r1 + 128));
    // psq_load w=0 quant=0 (using PPC_PsqL)
    f30.d = MemoryInline::ReadResolvedFloat64(guest_range_1, 36u, (r1 + 112));
    // psq_load w=0 quant=0 (using PPC_PsqL)
    f29.d = MemoryInline::ReadResolvedFloat64(guest_range_1, 20u, (r1 + 96));
    r11 = (r1 + 96);
    // inline leaf 0x802DCDC0 (6 guest instruction(s))
    {
        const auto resolved_pair = MemoryInline::ReadResolvedPair32(guest_range_1, 0u);
        if (resolved_pair.valid) {
            r27 = resolved_pair.first;
            r28 = resolved_pair.second;
        } else {
            r27 = MemoryInline::ReadResolved32(guest_range_1, 0u, (r11 + -20));
            r28 = MemoryInline::ReadResolved32(guest_range_1, 4u, (r11 + -16));
        }
    }
    {
        const auto resolved_pair = MemoryInline::ReadResolvedPair32(guest_range_1, 8u);
        if (resolved_pair.valid) {
            r29 = resolved_pair.first;
            r30 = resolved_pair.second;
        } else {
            r29 = MemoryInline::ReadResolved32(guest_range_1, 8u, (r11 + -12));
            r30 = MemoryInline::ReadResolved32(guest_range_1, 12u, (r11 + -8));
        }
    }
    r31 = MemoryInline::ReadResolved32(guest_range_1, 16u, (r11 + -4));
    // end of inlined leaf 0x802DCDC0
    r0 = MemoryInline::ReadResolved32(guest_range_1, 72u, (r1 + 148));
    ctx->lr = r0;
    r1 = (r1 + 144);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[8] = r8;
    ctx->gpr[11] = r11;
    ctx->gpr[27] = r27;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->fpr[3] = f3;
    ctx->fpr[4] = f4;
    ctx->fpr[5] = f5;
    ctx->fpr[6] = f6;
    ctx->fpr[29] = f29;
    ctx->fpr[30] = f30;
    ctx->fpr[31] = f31;
    ctx->cr = cr;
    return;
}

}