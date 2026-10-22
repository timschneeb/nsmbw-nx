## Task

* Port this recomp of the Wii game "New Super Mario Bros. Wii" to the Nintendo Switch (Homebrew, with devkitPro).
* You do not need to port the entire recompiler, just the compiled end-product should run on the Nintendo Switch.

## Style

* Do not be overly verbose and avoid adding unneeded comments.
* Keep comments concise.
* Do not use em-dashes.

## Installed tools

* devkitPro: /opt/devkitpro

## MCP

You can use nx-mcp to interact with the Switch.

Please note the following: Background sysmodules cannot directly start .nro files. You can launch a program by its program id, so only apps/games installed into the home menu/system can be launched. However, I have installed a forwarder application entry into the home menu, that chainloads our nsmbw nro file; program id: 05B6B05B8AF6F000.

For any other nros or test apps, you can do the following:
* Launch sphaira (program id: 5446530aca7e000)
* sphaira will provide a nxlink server that will allow you to remotely upload and execute an NRO file. Use the `nxlink` command for this. Example: `nxlink --path /switch/target_path.nro ./local_app_to_upload.nro`
* sphaira will also provide an FTP server on port 5000 while open, but please use nx-mcp.

Switch IP: 192.168.178.177

## References

I have cloned some other similar projects for the Switch under _references/.

* Twilight Princess Port Gamecube (this is a decomp, not a recomp however) (preferred reference, also uses aurora): https://github.com/souldbminerr/dusklight-nx
* Super Smash Bros Melee Gamecube (this is a decomp, not a recomp however, uses aurora too): https://github.com/KawaiiBunga/Melee-NX
* Wind Waker HD Recomp Wii U (not using Aurora however): https://github.com/centollOS/SwitchWakerHD

Currently, there is no Wii recomp or decomp that was ported to the Switch, so I don't have an exact reference for it. But, the Twilight Princess port is close enough and the most mature (though not a recomp).
