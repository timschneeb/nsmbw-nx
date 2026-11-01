extern "C" void NW4R_G3D_GetResNode_Translated(CpuContext* MKW_RESTRICT ctx)
{
    uint32_t cr0_0 = 0;
    uint32_t r3_rot_0 = 0;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];

    goto loc_8023B300;

loc_8023B300:
{
    r3 = MemoryInline::FlatRead32(r3);
    r0 = MemoryInline::FlatRead32((r3 + 20));
}

loc_8023B30C:
{
    if ((static_cast<int32_t>(r0) == static_cast<int32_t>(0))) {
        goto loc_8023B318;
    }
}

loc_8023B310:
{
    r0 = (r3 + r0);
    goto loc_8023B31C;
}

loc_8023B318:
{
    r0 = 0;
}

loc_8023B31C:
{
}

loc_8023B320:
{
    if ((static_cast<int32_t>(r0) == static_cast<int32_t>(0))) {
        goto loc_8023B34C;
    }
}

loc_8023B324:
{
    r3 = (r4 + 1);
    r3_rot_0 = PpcRotl32Inline(static_cast<uint32_t>(r3), static_cast<uint32_t>(4));
    r3 = (r3_rot_0 & -16);
    r3 = (r0 + r3);
    r3 = MemoryInline::FlatRead32((r3 + 20));
}

loc_8023B338:
{
    if ((static_cast<int32_t>(r3) == static_cast<int32_t>(0))) {
        goto loc_8023B344;
    }
}

loc_8023B33C:
{
    r3 = (r0 + r3);
    ctx->gpr[0] = r0;
    ctx->gpr[3] = r3;
    return;
}

loc_8023B344:
{
    r3 = 0;
    ctx->gpr[0] = r0;
    ctx->gpr[3] = r3;
    return;
}

loc_8023B34C:
{
    r3 = 0;
    ctx->gpr[0] = r0;
    ctx->gpr[3] = r3;
    return;
}

}