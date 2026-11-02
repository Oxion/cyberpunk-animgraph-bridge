set_xmakever("2.5.9")

set_project("AnimgraphBridge")
set_version("0.1.0", {build = "%y%m%d%H%M"})

set_arch("x64")
set_languages("cxx20")
add_cxxflags("/MP /GR- /EHsc")

-- Game path for `xmake deploy` only: env.local.lua (gitignored) or `xmake f --gamedir=...`
-- (xmake sandbox has no io — cannot parse .env)
if os.isfile(path.join(os.scriptdir(), "env.local.lua")) then
    includes("env.local.lua")
else
    option("gamedir")
        set_default(os.getenv("GAME_DIR") or os.getenv("CP2077") or "")
        set_showmenu(true)
        set_description("Cyberpunk 2077 game root (or create env.local.lua)")
    option_end()
end

if is_mode("debug") then
    set_symbols("debug")
    set_optimize("none")
    add_cxxflags("/Od /Ob0 /Zi /RTC1 /bigobj")
elseif is_mode("release") then
    set_symbols("hidden")
    set_strip("all")
    set_optimize("fastest")
    add_cxxflags("/Ob2")
elseif is_mode("releasedbg") then
    set_symbols("debug")
    set_strip("all")
    set_optimize("fastest")
    add_cxxflags("/Ob1 /Zi")
end

if is_mode("debug") then
    set_runtimes("MDd")
else
    set_runtimes("MD")
end

target("AnimgraphBridge")
    set_default(true)
    set_kind("shared")
    set_filename("animgraph-bridge.dll")
    set_pcxxheader("src/pch.hpp")
    add_files("src/**.cpp", "src/**.c", "src/**.rc")
    add_headerfiles("src/**.hpp", "src/**.h")
    add_includedirs("src/", "vendor/", "vendor/json/single_include")
    add_deps("RED4ext.SDK", "semver", "RedLib")
    add_syslinks("User32", "Ws2_32")
    add_defines("WINVER=0x0601", "WIN32_LEAN_AND_MEAN", "NOMINMAX")
    set_configdir("src")
    add_configfiles("config/Project.hpp.in", {prefixdir = "App"})
    add_configfiles("config/Version.rc.in", {prefixdir = "App"})
    set_configvar("AUTHOR", "animgraph-bridge")
    set_configvar("NAME", "AnimgraphBridge")

task("deploy")
    set_menu {
        usage = "xmake deploy",
        description = "Copy animgraph-bridge.dll + config.json into Cyberpunk 2077 (gamedir)",
    }
    on_run(function ()
        import("core.project.config")
        import("core.project.project")
        config.load()

        local root = config.get("gamedir")
        if not root or root == "" then
            raise("AnimgraphBridge: set gamedir in env.local.lua (see env.local.example.lua)")
        end

        local target = project.target("AnimgraphBridge")
        if not target then
            raise("AnimgraphBridge: target not found — run xmake build first")
        end
        local dll = target:targetfile()
        if not os.isfile(dll) then
            raise("AnimgraphBridge: missing " .. dll .. " — run xmake build first")
        end

        local dst = path.join(root, "red4ext", "plugins", "AnimgraphBridge")
        os.mkdir(dst)
        os.cp(dll, path.join(dst, "animgraph-bridge.dll"))
        local cfg = path.join(os.projectdir(), "config", "config.json")
        if os.isfile(cfg) then
            os.cp(cfg, path.join(dst, "config.json"))
        end
        print("AnimgraphBridge: deployed → " .. dst)
    end)
task_end()

target("RED4ext.SDK")
    set_default(false)
    set_kind("static")
    set_group("vendor")
    add_headerfiles("vendor/RED4ext.SDK/include/**.hpp")
    add_includedirs("vendor/RED4ext.SDK/include/", {public = true})

target("semver")
    set_default(false)
    set_kind("static")
    set_group("vendor")
    add_headerfiles("vendor/semver/include/**.hpp")
    add_includedirs("vendor/semver/include/", {public = true})

target("RedLib")
    set_default(false)
    set_kind("static")
    set_group("vendor")
    add_headerfiles("vendor/RedLib/include/**.hpp")
    add_includedirs("vendor/RedLib/include/", "vendor/RedLib/vendor/", {public = true})

add_rules("plugin.vsxmake.autoupdate")
