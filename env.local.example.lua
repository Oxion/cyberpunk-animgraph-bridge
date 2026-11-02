-- Copy to env.local.lua (gitignored).
-- Used by `xmake deploy` only (not by build/package).
option("gamedir")
    set_default([[E:/path_to_/Cyberpunk 2077]])
    set_showmenu(true)
    set_description("Cyberpunk 2077 game root")
option_end()
