extern "C" void ScnGroupDefG3dProcTranslated(CpuContext* MKW_RESTRICT ctx)
{
    uint32_t cr0_0 = 0;
    uint32_t r3_addr_0 = 0;
    uint32_t r3_addr_1 = 0;
    uint32_t r3_addr_2 = 0;
    uint32_t r3_addr_3 = 0;
    uint32_t r3_addr_4 = 0;
    uint32_t r3_addr_5 = 0;
    uint32_t r3_addr_6 = 0;
    uint32_t r3_addr_7 = 0;
    uint32_t r3_addr_8 = 0;
    uint32_t r3_addr_9 = 0;
    uint8_t* guest_range_0 = nullptr;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r1 = ctx->gpr[1];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r5 = ctx->gpr[5];
    uint32_t r6 = ctx->gpr[6];
    uint32_t r7 = ctx->gpr[7];
    uint32_t r11 = ctx->gpr[11];
    uint32_t r12 = ctx->gpr[12];
    uint32_t r26 = ctx->gpr[26];
    uint32_t r27 = ctx->gpr[27];
    uint32_t r28 = ctx->gpr[28];
    uint32_t r29 = ctx->gpr[29];
    uint32_t r30 = ctx->gpr[30];
    uint32_t r31 = ctx->gpr[31];
    uint32_t cr = ctx->cr;
    uint32_t ctr = ctx->ctr;
    uint32_t xer = ctx->xer;

    goto loc_80257FC0;

loc_80257FC0:
{
    guest_range_0 = MemoryInline::ResolveRangeHost((r1 + -48), 0, 56u, false, true);
    MemoryInline::WriteResolved32(guest_range_0, 0u, (r1 + -48), r1);
    r1 = (r1 + -48);
    r0 = ctx->lr;
    MemoryInline::WriteResolved32(guest_range_0, 52u, (r1 + 52), r0);
    r11 = (r1 + 48);
    // inline leaf 0x802DCD70 (7 guest instruction(s))
    if (!MemoryInline::WriteResolvedPair32(guest_range_0, 24u, ((static_cast<uint64_t>(static_cast<uint32_t>(r26)) << 32) | static_cast<uint32_t>(r27)))) {
        MemoryInline::WriteResolved32(guest_range_0, 24u, (r11 + -24), r26);
        MemoryInline::WriteResolved32(guest_range_0, 28u, (r11 + -20), r27);
    }
    if (!MemoryInline::WriteResolvedPair32(guest_range_0, 32u, ((static_cast<uint64_t>(static_cast<uint32_t>(r28)) << 32) | static_cast<uint32_t>(r29)))) {
        MemoryInline::WriteResolved32(guest_range_0, 32u, (r11 + -16), r28);
        MemoryInline::WriteResolved32(guest_range_0, 36u, (r11 + -12), r29);
    }
    if (!MemoryInline::WriteResolvedPair32(guest_range_0, 40u, ((static_cast<uint64_t>(static_cast<uint32_t>(r30)) << 32) | static_cast<uint32_t>(r31)))) {
        MemoryInline::WriteResolved32(guest_range_0, 40u, (r11 + -8), r30);
        MemoryInline::WriteResolved32(guest_range_0, 44u, (r11 + -4), r31);
    }
    // end of inlined leaf 0x802DCD70
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r4), static_cast<int32_t>(5));
}

loc_80257FD8:
{
    r29 = r3;
    r26 = r4;
    r30 = r5;
    r31 = r6;
    if (((cr & 0x20000000u) != 0)) {
        goto loc_80258050;
    }
}

loc_80257FEC:
{
    if (((cr & 0x80000000u) == 0)) {
        goto loc_80258014;
    }
}

loc_80257FF0:
{
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r4), static_cast<int32_t>(2));
}

loc_80257FF4:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_8025819C;
    }
}

loc_80257FF8:
{
    if (((cr & 0x80000000u) == 0)) {
        goto loc_80258008;
    }
}

loc_80257FFC:
{
}

loc_80258000:
{
    if ((static_cast<int32_t>(r4) >= static_cast<int32_t>(1))) {
        goto loc_80258060;
    }
}

loc_80258004:
{
    goto loc_802583C8;
}

loc_80258008:
{
}

loc_8025800C:
{
    if ((static_cast<int32_t>(r4) >= static_cast<int32_t>(4))) {
        goto loc_80258270;
    }
}

loc_80258010:
{
    goto loc_802583C8;
}

loc_80258014:
{
    r5 = 65536;
    r0 = (r5 + 2);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r4), static_cast<int32_t>(r0));
}

loc_80258020:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_802583C0;
    }
}

loc_80258024:
{
    if (((cr & 0x80000000u) == 0)) {
        goto loc_80258040;
    }
}

loc_80258028:
{
    r0 = (r5 + 1);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r4), static_cast<int32_t>(r0));
}

loc_80258030:
{
    if (((cr & 0x80000000u) == 0)) {
        goto loc_8025839C;
    }
}

loc_80258034:
{
}

loc_80258038:
{
    if ((static_cast<int32_t>(r4) >= static_cast<int32_t>(8))) {
        goto loc_802583C8;
    }
}

loc_8025803C:
{
    goto loc_8025840C;
}

loc_80258040:
{
    r0 = (r5 + 4);
}

loc_80258048:
{
    if ((static_cast<int32_t>(r4) >= static_cast<int32_t>(r0))) {
        goto loc_802583C8;
    }
}

loc_8025804C:
{
    goto loc_802583B4;
}

loc_80258050:
{
    r4 = r30;
    r5 = r31;
    ctx->lr = 0x8025805Cu;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[11] = r11;
    ctx->gpr[12] = r12;
    ctx->gpr[26] = r26;
    ctx->gpr[27] = r27;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    MKW_STATIC_TRANSLATED_CALL(0x80257E60u, func_80257E60, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r7 = ctx->gpr[7];
    r11 = ctx->gpr[11];
    r12 = ctx->gpr[12];
    r26 = ctx->gpr[26];
    r27 = ctx->gpr[27];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    goto loc_8025840C;
}

loc_80258060:
{
    MemoryInline::FlatWriteRam32((r1 + 8), r5);
    r4 = MemoryInline::FlatRead32((r3 + 212));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r4), static_cast<int32_t>(0));
}

loc_8025806C:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_802580AC;
    }
}

loc_80258070:
{
    r0 = MemoryInline::FlatRead16((r3 + 218));
    r0 = (r0 & 1);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_80258078:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_802580AC;
    }
}

loc_8025807C:
{
    r0 = MemoryInline::FlatRead8((r3 + 216));
    r0 = (r0 & 1);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_80258084:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_802580AC;
    }
}

loc_80258088:
{
    r12 = MemoryInline::FlatRead32(r4);
    r3 = r4;
    r5 = r29;
    r6 = r30;
    r12 = MemoryInline::FlatRead32((r12 + 12));
    r7 = r31;
    r4 = 1;
    ctr = r12;
    ctx->lr = 0x802580ACu;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[11] = r11;
    ctx->gpr[12] = r12;
    ctx->gpr[26] = r26;
    ctx->gpr[27] = r27;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
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
    r11 = ctx->gpr[11];
    r12 = ctx->gpr[12];
    r26 = ctx->gpr[26];
    r27 = ctx->gpr[27];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
}

loc_802580AC:
{
    r12 = MemoryInline::FlatRead32(r29);
    r3 = r29;
    r4 = r31;
    r5 = (r1 + 8);
    r12 = MemoryInline::FlatRead32((r12 + 48));
    ctr = r12;
    ctx->lr = 0x802580C8u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[11] = r11;
    ctx->gpr[12] = r12;
    ctx->gpr[26] = r26;
    ctx->gpr[27] = r27;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
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
    r11 = ctx->gpr[11];
    r12 = ctx->gpr[12];
    r26 = ctx->gpr[26];
    r27 = ctx->gpr[27];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r3 = MemoryInline::FlatRead32((r29 + 212));
    r6 = MemoryInline::FlatRead32((r1 + 8));
}

loc_802580D4:
{
    if ((static_cast<int32_t>(r3) == static_cast<int32_t>(0))) {
        goto loc_8025810C;
    }
}

loc_802580D8:
{
    r0 = MemoryInline::FlatRead16((r29 + 218));
    r0 = (r0 & 1);
}

loc_802580E0:
{
    if ((static_cast<int32_t>(r0) == static_cast<int32_t>(0))) {
        goto loc_8025810C;
    }
}

loc_802580E4:
{
    r0 = MemoryInline::FlatRead8((r29 + 216));
    r0 = (r0 & 2);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_802580EC:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_8025810C;
    }
}

loc_802580F0:
{
    r12 = MemoryInline::FlatRead32(r3);
    r5 = r29;
    r7 = r31;
    r4 = 2;
    r12 = MemoryInline::FlatRead32((r12 + 12));
    ctr = r12;
    ctx->lr = 0x8025810Cu;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[11] = r11;
    ctx->gpr[12] = r12;
    ctx->gpr[26] = r26;
    ctx->gpr[27] = r27;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
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
    r11 = ctx->gpr[11];
    r12 = ctx->gpr[12];
    r26 = ctx->gpr[26];
    r27 = ctx->gpr[27];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
}

loc_8025810C:
{
    r28 = (r29 + 60);
    r30 = 0;
    r27 = 0;
    goto loc_80258148;
}

loc_8025811C:
{
    r3 = MemoryInline::FlatRead32((r29 + 220));
    r6 = r28;
    r5 = MemoryInline::FlatRead32((r1 + 8));
    r4 = 1;
    r3_addr_2 = (r3 + r27);
    r3 = MemoryInline::FlatRead32(r3_addr_2);
    r12 = MemoryInline::FlatRead32(r3);
    r12 = MemoryInline::FlatRead32((r12 + 12));
    ctr = r12;
    ctx->lr = 0x80258140u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[11] = r11;
    ctx->gpr[12] = r12;
    ctx->gpr[26] = r26;
    ctx->gpr[27] = r27;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
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
    r11 = ctx->gpr[11];
    r12 = ctx->gpr[12];
    r26 = ctx->gpr[26];
    r27 = ctx->gpr[27];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r27 = (r27 + 4);
    r30 = (r30 + 1);
}

loc_80258148:
{
    r0 = MemoryInline::FlatRead32((r29 + 228));
    SetCRResident(cr, xer, 0, static_cast<uint32_t>(r30), static_cast<uint32_t>(r0));
}

loc_80258150:
{
    if (((cr & 0x80000000u) != 0)) {
        goto loc_8025811C;
    }
}

loc_80258154:
{
    r3 = MemoryInline::FlatRead32((r29 + 212));
    r6 = MemoryInline::FlatRead32((r1 + 8));
}

loc_80258160:
{
    if ((static_cast<int32_t>(r3) == static_cast<int32_t>(0))) {
        goto loc_8025840C;
    }
}

loc_80258164:
{
    r0 = MemoryInline::FlatRead16((r29 + 218));
    r0 = (r0 & 1);
}

loc_8025816C:
{
    if ((static_cast<int32_t>(r0) == static_cast<int32_t>(0))) {
        goto loc_8025840C;
    }
}

loc_80258170:
{
    r0 = MemoryInline::FlatRead8((r29 + 216));
    r0 = (r0 & 4);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_80258178:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_8025840C;
    }
}

loc_8025817C:
{
    r12 = MemoryInline::FlatRead32(r3);
    r5 = r29;
    r7 = r31;
    r4 = 4;
    r12 = MemoryInline::FlatRead32((r12 + 12));
    ctr = r12;
    ctx->lr = 0x80258198u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[11] = r11;
    ctx->gpr[12] = r12;
    ctx->gpr[26] = r26;
    ctx->gpr[27] = r27;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
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
    r11 = ctx->gpr[11];
    r12 = ctx->gpr[12];
    r26 = ctx->gpr[26];
    r27 = ctx->gpr[27];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    goto loc_8025840C;
}

loc_8025819C:
{
    r4 = MemoryInline::FlatRead32((r3 + 212));
}

loc_802581A4:
{
    if ((static_cast<int32_t>(r4) == static_cast<int32_t>(0))) {
        goto loc_802581E4;
    }
}

loc_802581A8:
{
    r0 = MemoryInline::FlatRead16((r3 + 218));
    r0 = (r0 & 2);
}

loc_802581B0:
{
    if ((static_cast<int32_t>(r0) == static_cast<int32_t>(0))) {
        goto loc_802581E4;
    }
}

loc_802581B4:
{
    r0 = MemoryInline::FlatRead8((r3 + 216));
    r0 = (r0 & 1);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_802581BC:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_802581E4;
    }
}

loc_802581C0:
{
    r12 = MemoryInline::FlatRead32(r4);
    r3 = r4;
    r5 = r29;
    r6 = r30;
    r12 = MemoryInline::FlatRead32((r12 + 16));
    r7 = r31;
    r4 = 1;
    ctr = r12;
    ctx->lr = 0x802581E4u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[11] = r11;
    ctx->gpr[12] = r12;
    ctx->gpr[26] = r26;
    ctx->gpr[27] = r27;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
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
    r11 = ctx->gpr[11];
    r12 = ctx->gpr[12];
    r26 = ctx->gpr[26];
    r27 = ctx->gpr[27];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
}

loc_802581E4:
{
    r28 = 0;
    r27 = 0;
    goto loc_8025821C;
}

loc_802581F0:
{
    r3 = MemoryInline::FlatRead32((r29 + 220));
    r5 = r30;
    r6 = r31;
    r4 = 2;
    r3_addr_6 = (r3 + r27);
    r3 = MemoryInline::FlatRead32(r3_addr_6);
    r12 = MemoryInline::FlatRead32(r3);
    r12 = MemoryInline::FlatRead32((r12 + 12));
    ctr = r12;
    ctx->lr = 0x80258214u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[11] = r11;
    ctx->gpr[12] = r12;
    ctx->gpr[26] = r26;
    ctx->gpr[27] = r27;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
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
    r11 = ctx->gpr[11];
    r12 = ctx->gpr[12];
    r26 = ctx->gpr[26];
    r27 = ctx->gpr[27];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r27 = (r27 + 4);
    r28 = (r28 + 1);
}

loc_8025821C:
{
    r0 = MemoryInline::FlatRead32((r29 + 228));
    SetCRResident(cr, xer, 0, static_cast<uint32_t>(r28), static_cast<uint32_t>(r0));
}

loc_80258224:
{
    if (((cr & 0x80000000u) != 0)) {
        goto loc_802581F0;
    }
}

loc_80258228:
{
    r3 = MemoryInline::FlatRead32((r29 + 212));
}

loc_80258230:
{
    if ((static_cast<int32_t>(r3) == static_cast<int32_t>(0))) {
        goto loc_8025840C;
    }
}

loc_80258234:
{
    r0 = MemoryInline::FlatRead16((r29 + 218));
    r0 = (r0 & 2);
}

loc_8025823C:
{
    if ((static_cast<int32_t>(r0) == static_cast<int32_t>(0))) {
        goto loc_8025840C;
    }
}

loc_80258240:
{
    r0 = MemoryInline::FlatRead8((r29 + 216));
    r0 = (r0 & 4);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_80258248:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_8025840C;
    }
}

loc_8025824C:
{
    r12 = MemoryInline::FlatRead32(r3);
    r5 = r29;
    r6 = r30;
    r7 = r31;
    r12 = MemoryInline::FlatRead32((r12 + 16));
    r4 = 4;
    ctr = r12;
    ctx->lr = 0x8025826Cu;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[11] = r11;
    ctx->gpr[12] = r12;
    ctx->gpr[26] = r26;
    ctx->gpr[27] = r27;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
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
    r11 = ctx->gpr[11];
    r12 = ctx->gpr[12];
    r26 = ctx->gpr[26];
    r27 = ctx->gpr[27];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    goto loc_8025840C;
}

loc_80258270:
{
    r4 = MemoryInline::FlatRead32((r3 + 212));
}

loc_80258278:
{
    if ((static_cast<int32_t>(r4) == static_cast<int32_t>(0))) {
        goto loc_802582B8;
    }
}

loc_8025827C:
{
    r0 = MemoryInline::FlatRead16((r3 + 218));
    r0 = (r0 & 4);
}

loc_80258284:
{
    if ((static_cast<int32_t>(r0) == static_cast<int32_t>(0))) {
        goto loc_802582B8;
    }
}

loc_80258288:
{
    r0 = MemoryInline::FlatRead8((r3 + 216));
    r0 = (r0 & 1);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_80258290:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_802582B8;
    }
}

loc_80258294:
{
    r12 = MemoryInline::FlatRead32(r4);
    r3 = r4;
    r5 = r29;
    r6 = r30;
    r12 = MemoryInline::FlatRead32((r12 + 20));
    r7 = r31;
    r4 = 1;
    ctr = r12;
    ctx->lr = 0x802582B8u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[11] = r11;
    ctx->gpr[12] = r12;
    ctx->gpr[26] = r26;
    ctx->gpr[27] = r27;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
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
    r11 = ctx->gpr[11];
    r12 = ctx->gpr[12];
    r26 = ctx->gpr[26];
    r27 = ctx->gpr[27];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
}

loc_802582B8:
{
    r3 = r31;
    r4 = (r29 + 60);
    r5 = (r29 + 108);
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    InvokeDirectCpuInContext<0x801C0540u>(ctx);
    r3 = MemoryInline::FlatRead32((r29 + 212));
}

loc_802582D0:
{
    if ((static_cast<int32_t>(r3) == static_cast<int32_t>(0))) {
        goto loc_8025830C;
    }
}

loc_802582D4:
{
    r0 = MemoryInline::FlatRead16((r29 + 218));
    r0 = (r0 & 4);
}

loc_802582DC:
{
    if ((static_cast<int32_t>(r0) == static_cast<int32_t>(0))) {
        goto loc_8025830C;
    }
}

loc_802582E0:
{
    r0 = MemoryInline::FlatRead8((r29 + 216));
    r0 = (r0 & 2);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_802582E8:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_8025830C;
    }
}

loc_802582EC:
{
    r12 = MemoryInline::FlatRead32(r3);
    r5 = r29;
    r6 = r30;
    r7 = r31;
    r12 = MemoryInline::FlatRead32((r12 + 20));
    r4 = 2;
    ctr = r12;
    ctx->lr = 0x8025830Cu;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[11] = r11;
    ctx->gpr[12] = r12;
    ctx->gpr[26] = r26;
    ctx->gpr[27] = r27;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
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
    r11 = ctx->gpr[11];
    r12 = ctx->gpr[12];
    r26 = ctx->gpr[26];
    r27 = ctx->gpr[27];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
}

loc_8025830C:
{
    r28 = 0;
    r27 = 0;
    goto loc_80258344;
}

loc_80258318:
{
    r3 = MemoryInline::FlatRead32((r29 + 220));
    r5 = r30;
    r6 = r31;
    r4 = 4;
    r3_addr_4 = (r3 + r27);
    r3 = MemoryInline::FlatRead32(r3_addr_4);
    r12 = MemoryInline::FlatRead32(r3);
    r12 = MemoryInline::FlatRead32((r12 + 12));
    ctr = r12;
    ctx->lr = 0x8025833Cu;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[11] = r11;
    ctx->gpr[12] = r12;
    ctx->gpr[26] = r26;
    ctx->gpr[27] = r27;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
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
    r11 = ctx->gpr[11];
    r12 = ctx->gpr[12];
    r26 = ctx->gpr[26];
    r27 = ctx->gpr[27];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r27 = (r27 + 4);
    r28 = (r28 + 1);
}

loc_80258344:
{
    r0 = MemoryInline::FlatRead32((r29 + 228));
    SetCRResident(cr, xer, 0, static_cast<uint32_t>(r28), static_cast<uint32_t>(r0));
}

loc_8025834C:
{
    if (((cr & 0x80000000u) != 0)) {
        goto loc_80258318;
    }
}

loc_80258350:
{
    r3 = MemoryInline::FlatRead32((r29 + 212));
}

loc_80258358:
{
    if ((static_cast<int32_t>(r3) == static_cast<int32_t>(0))) {
        goto loc_8025840C;
    }
}

loc_8025835C:
{
    r0 = MemoryInline::FlatRead16((r29 + 218));
    r0 = (r0 & 4);
}

loc_80258364:
{
    if ((static_cast<int32_t>(r0) == static_cast<int32_t>(0))) {
        goto loc_8025840C;
    }
}

loc_80258368:
{
    r0 = MemoryInline::FlatRead8((r29 + 216));
    r0 = (r0 & 4);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_80258370:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_8025840C;
    }
}

loc_80258374:
{
    r12 = MemoryInline::FlatRead32(r3);
    r5 = r29;
    r6 = r30;
    r7 = r31;
    r12 = MemoryInline::FlatRead32((r12 + 20));
    r4 = 4;
    ctr = r12;
    ctx->lr = 0x80258394u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[11] = r11;
    ctx->gpr[12] = r12;
    ctx->gpr[26] = r26;
    ctx->gpr[27] = r27;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
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
    r11 = ctx->gpr[11];
    r12 = ctx->gpr[12];
    r26 = ctx->gpr[26];
    r27 = ctx->gpr[27];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    goto loc_8025840C;
}

loc_8025839C:
{
    r12 = MemoryInline::FlatRead32(r3);
    r4 = r31;
    r12 = MemoryInline::FlatRead32((r12 + 60));
    ctr = r12;
    ctx->lr = 0x802583B0u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[11] = r11;
    ctx->gpr[12] = r12;
    ctx->gpr[26] = r26;
    ctx->gpr[27] = r27;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
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
    r11 = ctx->gpr[11];
    r12 = ctx->gpr[12];
    r26 = ctx->gpr[26];
    r27 = ctx->gpr[27];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    goto loc_8025840C;
}

loc_802583B4:
{
    r0 = 0;
    MemoryInline::FlatWrite32((r3 + 4), r0);
    goto loc_8025840C;
}

loc_802583C0:
{
    MemoryInline::FlatWrite32((r3 + 4), r6);
    goto loc_8025840C;
}

loc_802583C8:
{
    r27 = 0;
    r28 = 0;
    goto loc_80258400;
}

loc_802583D4:
{
    r3 = MemoryInline::FlatRead32((r29 + 220));
    r4 = r26;
    r5 = r30;
    r6 = r31;
    r3_addr_8 = (r3 + r28);
    r3 = MemoryInline::FlatRead32(r3_addr_8);
    r12 = MemoryInline::FlatRead32(r3);
    r12 = MemoryInline::FlatRead32((r12 + 12));
    ctr = r12;
    ctx->lr = 0x802583F8u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[11] = r11;
    ctx->gpr[12] = r12;
    ctx->gpr[26] = r26;
    ctx->gpr[27] = r27;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
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
    r11 = ctx->gpr[11];
    r12 = ctx->gpr[12];
    r26 = ctx->gpr[26];
    r27 = ctx->gpr[27];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r28 = (r28 + 4);
    r27 = (r27 + 1);
}

loc_80258400:
{
    r0 = MemoryInline::FlatRead32((r29 + 228));
    SetCRResident(cr, xer, 0, static_cast<uint32_t>(r27), static_cast<uint32_t>(r0));
}

loc_80258408:
{
    if (((cr & 0x80000000u) != 0)) {
        goto loc_802583D4;
    }
}

loc_8025840C:
{
    r11 = (r1 + 48);
    // inline leaf 0x802DCDBC (7 guest instruction(s))
    r26 = MemoryInline::FlatRead32((r11 + -24));
    r27 = MemoryInline::FlatRead32((r11 + -20));
    r28 = MemoryInline::FlatRead32((r11 + -16));
    r29 = MemoryInline::FlatRead32((r11 + -12));
    r30 = MemoryInline::FlatRead32((r11 + -8));
    r31 = MemoryInline::FlatRead32((r11 + -4));
    // end of inlined leaf 0x802DCDBC
    r0 = MemoryInline::FlatRead32((r1 + 52));
    ctx->lr = r0;
    r1 = (r1 + 48);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[11] = r11;
    ctx->gpr[12] = r12;
    ctx->gpr[26] = r26;
    ctx->gpr[27] = r27;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    return;
}

}
