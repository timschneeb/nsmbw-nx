extern "C" void CXReadUncompLZTranslated(CpuContext* MKW_RESTRICT ctx)
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
    uint32_t r0_subfic_ra_0 = 0;
    uint32_t r0_subfic_ra_1 = 0;
    uint32_t r0_subfic_ra_2 = 0;
    uint32_t r0_subfic_ra_3 = 0;
    uint32_t r0_subfic_ra_4 = 0;
    uint32_t r0_subfic_ra_5 = 0;
    uint32_t r0_subfic_ra_6 = 0;
    uint32_t r0_subfic_ra_7 = 0;
    uint32_t r10_addic_src_0 = 0;
    uint32_t r10_addic_src_1 = 0;
    uint32_t r10_addic_src_2 = 0;
    uint32_t r10_addic_src_3 = 0;
    uint32_t r10_addic_src_4 = 0;
    uint32_t r10_addic_src_5 = 0;
    uint32_t r5_addic_src_0 = 0;
    uint32_t r5_addic_src_1 = 0;
    uint32_t r5_addic_src_2 = 0;
    uint32_t r5_addic_src_3 = 0;
    uint32_t r5_addic_src_4 = 0;
    uint32_t r5_addic_src_5 = 0;
    uint32_t r5_addic_src_6 = 0;
    uint32_t r5_addic_src_7 = 0;
    uint32_t r7_mdest_0 = 0;
    uint32_t r7_mdest_1 = 0;
    uint32_t r7_mdest_2 = 0;
    uint32_t r7_mdest_3 = 0;
    uint32_t r7_mdest_4 = 0;
    uint32_t r7_mdest_5 = 0;
    uint32_t r7_mdest_6 = 0;
    uint32_t r7_mrot_0 = 0;
    uint32_t r7_mrot_1 = 0;
    uint32_t r7_mrot_2 = 0;
    uint32_t r7_mrot_3 = 0;
    uint32_t r7_mrot_4 = 0;
    uint32_t r7_mrot_5 = 0;
    uint32_t r7_mrot_6 = 0;
    uint32_t r7_rot_0 = 0;
    uint32_t r7_rot_1 = 0;
    uint32_t r7_rot_10 = 0;
    uint32_t r7_rot_11 = 0;
    uint32_t r7_rot_2 = 0;
    uint32_t r7_rot_3 = 0;
    uint32_t r7_rot_4 = 0;
    uint32_t r7_rot_5 = 0;
    uint32_t r7_rot_6 = 0;
    uint32_t r7_rot_7 = 0;
    uint32_t r7_rot_8 = 0;
    uint32_t r7_rot_9 = 0;
    uint32_t r9_rot_0 = 0;
    uint32_t r9_rot_1 = 0;
    uint32_t r9_rot_2 = 0;
    uint32_t r9_rot_3 = 0;
    uint32_t r9_rot_4 = 0;
    uint32_t r9_rot_5 = 0;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r1 = ctx->gpr[1];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r5 = ctx->gpr[5];
    uint32_t r6 = ctx->gpr[6];
    uint32_t r7 = ctx->gpr[7];
    uint32_t r8 = ctx->gpr[8];
    uint32_t r9 = ctx->gpr[9];
    uint32_t r10 = ctx->gpr[10];
    uint32_t r11 = ctx->gpr[11];
    uint32_t r12 = ctx->gpr[12];
    uint32_t r31 = ctx->gpr[31];
    uint32_t cr = ctx->cr;
    uint32_t xer = ctx->xer;

    goto loc_801D6B60;

loc_801D6B60:
{
    MemoryInline::FlatWriteRam32((r1 + -16), r1);
    r1 = (r1 + -16);
    MemoryInline::FlatWriteRam32((r1 + 12), r31);
    r0 = MemoryInline::FlatRead8((r3 + 19));
}

loc_801D6B70:
{
    if ((static_cast<int32_t>(r0) == static_cast<int32_t>(0))) {
        goto loc_801D6CAC;
    }
}

loc_801D6B74:
{
}

loc_801D6B78:
{
    if ((static_cast<uint32_t>(r0) != static_cast<uint32_t>(8))) {
        goto loc_801D6BAC;
    }
}

loc_801D6B7C:
{
    r6 = MemoryInline::FlatRead8(r4);
    r0 = (r6 & 240);
}

loc_801D6B88:
{
    if ((static_cast<int32_t>(r0) == static_cast<int32_t>(16))) {
        goto loc_801D6B94;
    }
}

loc_801D6B8C:
{
    r3 = -1;
    goto loc_801D6F14;
}

loc_801D6B94:
{
    r0 = (r6 & 15);
    MemoryInline::FlatWrite8((r3 + 20), static_cast<uint8_t>(r0));
}

loc_801D6BA0:
{
    if ((static_cast<uint32_t>(r0) <= static_cast<uint32_t>(1))) {
        goto loc_801D6BAC;
    }
}

loc_801D6BA4:
{
    r3 = -1;
    goto loc_801D6F14;
}

loc_801D6BAC:
{
    r11 = MemoryInline::FlatRead32((r3 + 8));
    r10 = r5;
    r9 = r4;
    r12 = 0;
    r6 = 0;
    goto loc_801D6C60;
}

loc_801D6BC8:
{
    r7 = MemoryInline::FlatRead8((r3 + 19));
    r0 = (r7 + -1);
    MemoryInline::FlatWrite8((r3 + 19), static_cast<uint8_t>(r0));
    r0 = (r0 & 255);
}

loc_801D6BDC:
{
    if ((static_cast<uint32_t>(r0) > static_cast<uint32_t>(3))) {
        goto loc_801D6C00;
    }
}

loc_801D6BE0:
{
    r0_subfic_ra_2 = r0;
    r0 = (3 - r0_subfic_ra_2);
    xer = (xer & 0xDFFFFFFFu) | ((static_cast<uint32_t>(3) >= static_cast<uint32_t>(r0_subfic_ra_2) ? 1u : 0u) << 29);
    r7 = MemoryInline::FlatRead8(r9);
    r0_rot_2 = PpcRotl32Inline(static_cast<uint32_t>(r0), static_cast<uint32_t>(3));
    r0 = (r0_rot_2 & -8);
    r8 = MemoryInline::FlatRead32((r3 + 4));
    r0 = PPC_Slw(static_cast<uint32_t>(r7), static_cast<uint32_t>(r0));
    r0 = (r8 | r0);
    MemoryInline::FlatWrite32((r3 + 4), r0);
    goto loc_801D6C24;
}

loc_801D6C00:
{
}

loc_801D6C04:
{
    if ((static_cast<uint32_t>(r0) > static_cast<uint32_t>(6))) {
        goto loc_801D6C24;
    }
}

loc_801D6C08:
{
    r0_subfic_ra_3 = r0;
    r0 = (6 - r0_subfic_ra_3);
    xer = (xer & 0xDFFFFFFFu) | ((static_cast<uint32_t>(6) >= static_cast<uint32_t>(r0_subfic_ra_3) ? 1u : 0u) << 29);
    r7 = MemoryInline::FlatRead8(r9);
    r0_rot_3 = PpcRotl32Inline(static_cast<uint32_t>(r0), static_cast<uint32_t>(3));
    r0 = (r0_rot_3 & -8);
    r8 = MemoryInline::FlatRead32((r3 + 4));
    r0 = PPC_Slw(static_cast<uint32_t>(r7), static_cast<uint32_t>(r0));
    r0 = (r8 | r0);
    MemoryInline::FlatWrite32((r3 + 4), r0);
}

loc_801D6C24:
{
    r0 = MemoryInline::FlatRead8((r3 + 19));
    r9 = (r9 + 1);
    r12 = (r12 + 1);
}

loc_801D6C34:
{
    if ((static_cast<uint32_t>(r0) != static_cast<uint32_t>(4))) {
        goto loc_801D6C48;
    }
}

loc_801D6C38:
{
    r0 = MemoryInline::FlatRead32((r3 + 4));
}

loc_801D6C40:
{
    if ((static_cast<int32_t>(r0) <= static_cast<int32_t>(0))) {
        goto loc_801D6C48;
    }
}

loc_801D6C44:
{
    MemoryInline::FlatWrite8((r3 + 19), static_cast<uint8_t>(r6));
}

loc_801D6C48:
{
    r10_addic_src_2 = r10;
    r10 = (r10_addic_src_2 + -1);
    {
        const uint64_t ppcCarryWide = static_cast<uint64_t>(static_cast<uint32_t>(r10_addic_src_2)) + static_cast<uint64_t>(static_cast<uint32_t>(-1)) + (static_cast<uint64_t>(static_cast<uint32_t>(0)) & 1u);
        xer = (xer & 0xDFFFFFFFu) | (static_cast<uint32_t>((ppcCarryWide >> 32) & 1u) << 29);
    }
}

loc_801D6C4C:
{
    if ((static_cast<int32_t>(r10) != static_cast<int32_t>(0))) {
        goto loc_801D6C60;
    }
}

loc_801D6C50:
{
    r0 = MemoryInline::FlatRead8((r3 + 19));
}

loc_801D6C58:
{
    if ((static_cast<int32_t>(r0) == static_cast<int32_t>(0))) {
        goto loc_801D6C60;
    }
}

loc_801D6C5C:
{
    goto loc_801D6C84;
}

loc_801D6C60:
{
    r0 = MemoryInline::FlatRead8((r3 + 19));
}

loc_801D6C68:
{
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(0))) {
        goto loc_801D6BC8;
    }
}

loc_801D6C6C:
{
}

loc_801D6C70:
{
    if ((static_cast<int32_t>(r11) <= static_cast<int32_t>(0))) {
        goto loc_801D6C84;
    }
}

loc_801D6C74:
{
    r0 = MemoryInline::FlatRead32((r3 + 4));
}

loc_801D6C7C:
{
    if ((static_cast<int32_t>(r11) >= static_cast<int32_t>(r0))) {
        goto loc_801D6C84;
    }
}

loc_801D6C80:
{
    MemoryInline::FlatWrite32((r3 + 4), r11);
}

loc_801D6C84:
{
    r5 = (r5 - r12);
    r4 = (r4 + r12);
    if ((static_cast<int32_t>(r5) != static_cast<int32_t>(0))) {
        goto loc_801D6CAC;
    }
}

loc_801D6C90:
{
    r0 = MemoryInline::FlatRead8((r3 + 19));
}

loc_801D6C98:
{
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(0))) {
        goto loc_801D6CA4;
    }
}

loc_801D6C9C:
{
    r3 = MemoryInline::FlatRead32((r3 + 4));
    goto loc_801D6F14;
}

loc_801D6CA4:
{
    r3 = -1;
    goto loc_801D6F14;
}

loc_801D6CAC:
{
    r0 = 0;
    r6 = 1;
    r11 = 3;
    r8 = 8;
    goto loc_801D6EE8;
}

loc_801D6CC8:
{
}

loc_801D6CCC:
{
    if ((static_cast<int32_t>(r5) != static_cast<int32_t>(0))) {
        goto loc_801D6CD8;
    }
}

loc_801D6CD0:
{
    r3 = MemoryInline::FlatRead32((r3 + 4));
    goto loc_801D6F14;
}

loc_801D6CD8:
{
    r7 = MemoryInline::FlatRead8((r3 + 17));
    r7 = (r7 & 128);
}

loc_801D6CE0:
{
    if ((static_cast<int32_t>(r7) != static_cast<int32_t>(0))) {
        goto loc_801D6DFC;
    }
}

loc_801D6CE4:
{
    r7 = MemoryInline::FlatRead32(r3);
    r5 = (r5 + -1);
    r9 = MemoryInline::FlatRead8(r4);
    r4 = (r4 + 1);
    MemoryInline::FlatWrite8(r7, static_cast<uint8_t>(r9));
    r9 = (r7 + 1);
    r7 = MemoryInline::FlatRead32((r3 + 4));
    MemoryInline::FlatWrite32(r3, r9);
    r7 = (r7 + -1);
    MemoryInline::FlatWrite32((r3 + 4), r7);
    goto loc_801D6E94;
}

loc_801D6D18:
{
    r7 = MemoryInline::FlatRead8((r3 + 20));
    r9 = MemoryInline::FlatRead8((r3 + 16));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r7), static_cast<int32_t>(0));
}

loc_801D6D24:
{
    r7 = (r9 + -1);
    MemoryInline::FlatWrite8((r3 + 16), static_cast<uint8_t>(r7));
    if (((cr & 0x20000000u) == 0)) {
        goto loc_801D6D48;
    }
}

loc_801D6D30:
{
    r7 = MemoryInline::FlatRead8(r4);
    r4 = (r4 + 1);
    MemoryInline::FlatWrite8((r3 + 16), static_cast<uint8_t>(r0));
    r7 = (r7 + 48);
    MemoryInline::FlatWrite32((r3 + 12), r7);
    goto loc_801D6DEC;
}

loc_801D6D48:
{
    r7 = (r7 & 255);
}

loc_801D6D50:
{
    if ((static_cast<int32_t>(r7) == static_cast<int32_t>(2))) {
        goto loc_801D6D68;
    }
}

loc_801D6D54:
{
}

loc_801D6D58:
{
    if ((static_cast<int32_t>(r7) == static_cast<int32_t>(1))) {
        goto loc_801D6DBC;
    }
}

loc_801D6D5C:
{
}

loc_801D6D60:
{
    if ((static_cast<int32_t>(r7) == static_cast<int32_t>(0))) {
        goto loc_801D6DD8;
    }
}

loc_801D6D64:
{
    goto loc_801D6DEC;
}

loc_801D6D68:
{
    r7 = MemoryInline::FlatRead8(r4);
    r4 = (r4 + 1);
    MemoryInline::FlatWrite32((r3 + 12), r7);
    {
        const uint32_t ppcCarryValue = static_cast<uint32_t>(r7);
        const uint32_t ppcCarryShift = static_cast<uint32_t>(4) & 0x3Fu;
        const bool ppcCarryNegative = (ppcCarryValue & 0x80000000u) != 0;
        const uint32_t ppcCarry = ppcCarryShift == 0 ? 0u : (ppcCarryShift >= 32 ? (ppcCarryNegative && ppcCarryValue != 0 ? 1u : 0u) : (ppcCarryNegative && (ppcCarryValue & ((1u << ppcCarryShift) - 1u)) != 0 ? 1u : 0u));
        xer = (xer & 0xDFFFFFFFu) | (ppcCarry << 29);
    }
    r9 = (static_cast<int32_t>(r7) >> 4);
}

loc_801D6D7C:
{
    if ((static_cast<int32_t>(r9) != static_cast<int32_t>(1))) {
        goto loc_801D6D90;
    }
}

loc_801D6D80:
{
    r7_rot_5 = PpcRotl32Inline(static_cast<uint32_t>(r7), static_cast<uint32_t>(16));
    r7 = (r7_rot_5 & 983040);
    r7 = (r7 + 4368);
    MemoryInline::FlatWrite32((r3 + 12), r7);
    goto loc_801D6DEC;
}

loc_801D6D90:
{
}

loc_801D6D94:
{
    if ((static_cast<int32_t>(r9) != static_cast<int32_t>(0))) {
        goto loc_801D6DAC;
    }
}

loc_801D6D98:
{
    r7_rot_6 = PpcRotl32Inline(static_cast<uint32_t>(r7), static_cast<uint32_t>(8));
    r7 = (r7_rot_6 & 3840);
    MemoryInline::FlatWrite8((r3 + 16), static_cast<uint8_t>(r6));
    r7 = (r7 + 272);
    MemoryInline::FlatWrite32((r3 + 12), r7);
    goto loc_801D6DEC;
}

loc_801D6DAC:
{
    r7 = (r7 + 16);
    MemoryInline::FlatWrite32((r3 + 12), r7);
    MemoryInline::FlatWrite8((r3 + 16), static_cast<uint8_t>(r0));
    goto loc_801D6DEC;
}

loc_801D6DBC:
{
    r7 = MemoryInline::FlatRead8(r4);
    r4 = (r4 + 1);
    r9 = MemoryInline::FlatRead32((r3 + 12));
    r7_rot_4 = PpcRotl32Inline(static_cast<uint32_t>(r7), static_cast<uint32_t>(8));
    r7 = (r7_rot_4 & -256);
    r7 = (r9 + r7);
    MemoryInline::FlatWrite32((r3 + 12), r7);
    goto loc_801D6DEC;
}

loc_801D6DD8:
{
    r7 = MemoryInline::FlatRead8(r4);
    r4 = (r4 + 1);
    r9 = MemoryInline::FlatRead32((r3 + 12));
    r7 = (r9 + r7);
    MemoryInline::FlatWrite32((r3 + 12), r7);
}

loc_801D6DEC:
{
    r5_addic_src_4 = r5;
    r5 = (r5_addic_src_4 + -1);
    {
        const uint64_t ppcCarryWide = static_cast<uint64_t>(static_cast<uint32_t>(r5_addic_src_4)) + static_cast<uint64_t>(static_cast<uint32_t>(-1)) + (static_cast<uint64_t>(static_cast<uint32_t>(0)) & 1u);
        xer = (xer & 0xDFFFFFFFu) | (static_cast<uint32_t>((ppcCarryWide >> 32) & 1u) << 29);
    }
}

loc_801D6DF0:
{
    if ((static_cast<int32_t>(r5) != static_cast<int32_t>(0))) {
        goto loc_801D6DFC;
    }
}

loc_801D6DF4:
{
    r3 = MemoryInline::FlatRead32((r3 + 4));
    goto loc_801D6F14;
}

loc_801D6DFC:
{
    r7 = MemoryInline::FlatRead8((r3 + 16));
}

loc_801D6E04:
{
    if ((static_cast<int32_t>(r7) != static_cast<int32_t>(0))) {
        goto loc_801D6D18;
    }
}

loc_801D6E08:
{
    r10 = MemoryInline::FlatRead32((r3 + 12));
    r5 = (r5 + -1);
    r12 = MemoryInline::FlatRead32((r3 + 4));
    {
        const uint32_t ppcCarryValue = static_cast<uint32_t>(r10);
        const uint32_t ppcCarryShift = static_cast<uint32_t>(4) & 0x3Fu;
        const bool ppcCarryNegative = (ppcCarryValue & 0x80000000u) != 0;
        const uint32_t ppcCarry = ppcCarryShift == 0 ? 0u : (ppcCarryShift >= 32 ? (ppcCarryNegative && ppcCarryValue != 0 ? 1u : 0u) : (ppcCarryNegative && (ppcCarryValue & ((1u << ppcCarryShift) - 1u)) != 0 ? 1u : 0u));
        xer = (xer & 0xDFFFFFFFu) | (ppcCarry << 29);
    }
    r9 = (static_cast<int32_t>(r10) >> 4);
    r7 = MemoryInline::FlatRead8(r4);
}

loc_801D6E20:
{
    r7_rot_8 = PpcRotl32Inline(static_cast<uint32_t>(r10), static_cast<uint32_t>(8));
    r7_mrot_3 = (r7_rot_8 & 3840);
    r7_mdest_3 = (r7 & -3841);
    r7 = (r7_mdest_3 | r7_mrot_3);
    MemoryInline::FlatWrite32((r3 + 12), r9);
    r31 = (r7 + 1);
    r4 = (r4 + 1);
    MemoryInline::FlatWrite8((r3 + 16), static_cast<uint8_t>(r11));
    if ((static_cast<int32_t>(r9) <= static_cast<int32_t>(r12))) {
        goto loc_801D6E88;
    }
}

loc_801D6E38:
{
    r7 = MemoryInline::FlatRead32((r3 + 8));
}

loc_801D6E40:
{
    if ((static_cast<int32_t>(r7) != static_cast<int32_t>(0))) {
        goto loc_801D6E4C;
    }
}

loc_801D6E44:
{
    r3 = -4;
    goto loc_801D6F14;
}

loc_801D6E4C:
{
    MemoryInline::FlatWrite32((r3 + 12), r12);
    goto loc_801D6E88;
}

loc_801D6E54:
{
    r9 = MemoryInline::FlatRead32(r3);
    r7 = (r9 - r31);
    r7 = MemoryInline::FlatRead8(r7);
    MemoryInline::FlatWrite8(r9, static_cast<uint8_t>(r7));
    r10 = MemoryInline::FlatRead32(r3);
    r9 = MemoryInline::FlatRead32((r3 + 4));
    r7 = MemoryInline::FlatRead32((r3 + 12));
    r10 = (r10 + 1);
    r9 = (r9 + -1);
    MemoryInline::FlatWrite32(r3, r10);
    r7 = (r7 + -1);
    MemoryInline::FlatWrite32((r3 + 4), r9);
    MemoryInline::FlatWrite32((r3 + 12), r7);
}

loc_801D6E88:
{
    r7 = MemoryInline::FlatRead32((r3 + 12));
}

loc_801D6E90:
{
    if ((static_cast<int32_t>(r7) > static_cast<int32_t>(0))) {
        goto loc_801D6E54;
    }
}

loc_801D6E94:
{
    r7 = MemoryInline::FlatRead32((r3 + 4));
}

loc_801D6E9C:
{
    if ((static_cast<int32_t>(r7) == static_cast<int32_t>(0))) {
        goto loc_801D6EF4;
    }
}

loc_801D6EA0:
{
    r9 = MemoryInline::FlatRead8((r3 + 17));
    r7 = MemoryInline::FlatRead8((r3 + 18));
    r9_rot_3 = PpcRotl32Inline(static_cast<uint32_t>(r9), static_cast<uint32_t>(1));
    r9 = (r9_rot_3 & 254);
    MemoryInline::FlatWrite8((r3 + 17), static_cast<uint8_t>(r9));
    r7 = (r7 + -1);
    MemoryInline::FlatWrite8((r3 + 18), static_cast<uint8_t>(r7));
}

loc_801D6EB8:
{
    r7 = MemoryInline::FlatRead8((r3 + 18));
}

loc_801D6EC0:
{
    if ((static_cast<int32_t>(r7) != static_cast<int32_t>(0))) {
        goto loc_801D6CC8;
    }
}

loc_801D6EC4:
{
}

loc_801D6EC8:
{
    if ((static_cast<int32_t>(r5) != static_cast<int32_t>(0))) {
        goto loc_801D6ED4;
    }
}

loc_801D6ECC:
{
    r3 = MemoryInline::FlatRead32((r3 + 4));
    goto loc_801D6F14;
}

loc_801D6ED4:
{
    r7 = MemoryInline::FlatRead8(r4);
    r5 = (r5 + -1);
    MemoryInline::FlatWrite8((r3 + 17), static_cast<uint8_t>(r7));
    r4 = (r4 + 1);
    MemoryInline::FlatWrite8((r3 + 18), static_cast<uint8_t>(r8));
}

loc_801D6EE8:
{
    r7 = MemoryInline::FlatRead32((r3 + 4));
}

loc_801D6EF0:
{
    if ((static_cast<int32_t>(r7) > static_cast<int32_t>(0))) {
        goto loc_801D6EB8;
    }
}

loc_801D6EF4:
{
    r0 = MemoryInline::FlatRead32((r3 + 8));
}

loc_801D6EFC:
{
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(0))) {
        goto loc_801D6F10;
    }
}

loc_801D6F00:
{
}

loc_801D6F04:
{
    if ((static_cast<uint32_t>(r5) <= static_cast<uint32_t>(32))) {
        goto loc_801D6F10;
    }
}

loc_801D6F08:
{
    r3 = -3;
    goto loc_801D6F14;
}

loc_801D6F10:
{
    r3 = 0;
}

loc_801D6F14:
{
    r31 = MemoryInline::FlatRead32((r1 + 12));
    r1 = (r1 + 16);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[8] = r8;
    ctx->gpr[9] = r9;
    ctx->gpr[10] = r10;
    ctx->gpr[11] = r11;
    ctx->gpr[12] = r12;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->xer = xer;
    return;
}

}
