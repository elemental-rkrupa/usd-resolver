newoption {
    trigger     = "platform-host",
    description = "(Optional) Specify host platform for cross-compilation"
}

newoption {
    trigger     = "usd-flavor",
    description = "Specify the USD flavor to link against"
}

newoption {
    trigger     = "usd-ver",
    description = "Specify the USD version to link against"
}

newoption {
    trigger     = "python-ver",
    description = "Specify the python version to link against",
}

newoption {
    trigger     = "local-path",
    description = "Specify a local path to an install of USD"
}

local pylibs = {
    ["0"] = "",
    ["39"] = "python3.9",
    ["310"] = "python3.10",
    ["311"] = "python3.11"
}

-- Include omni.repo.build premake tools
repo_build = require("omni/repo/build")

-- Enable /sourcelink flag for VS
repo_build.enable_vstudio_sourcelink()

-- Remove /JMC parameter for visual studio
repo_build.remove_vstudio_jmc()

-- Configure the options for the build
repo_build.setup_options()

repo_build.root = repo_build.get_abs_path(os.getcwd())

-- Include repo_usd premake templates
repo_usd = require("_build/repo-deps/repo_usd/templates/premake/premake5-usd")

function copy_to_file(filePath, newPath)
    local filePathAbs = repo_build.get_abs_path(filePath)
    local dir = newPath:match("(.*[\\/])")
    if os.target() == "windows" then
        if dir ~= "" then
            --dir = dir:gsub('/', '\\')
            postbuildcommands { "{MKDIR} \""..dir.."\"" }
        end
        -- Using {COPY} on Windows adds an IF EXIST with an extra backslash which doesn't work
        filePathAbs = filePathAbs:gsub('/', '\\')
        newPath = newPath:gsub('/', '\\')
        postbuildcommands { "copy /Y \""..filePathAbs.."\" \""..newPath.."\"" }
    else
        if dir ~= "" then
            postbuildcommands { "$(SILENT) {MKDIR} "..dir }
        end
        postbuildcommands { "$(SILENT) {COPY} "..filePathAbs.." "..newPath }
    end
end

function os.capture(cmd, raw)
  local f = assert(io.popen(cmd, 'r'))
  local s = assert(f:read('*a'))
  f:close()
  if raw then return s end
  s = string.gsub(s, '^%s+', '')
  s = string.gsub(s, '%s+$', '')
  s = string.gsub(s, '[\n\r]+', ' ')
  return s
end

premake.override(premake.vstudio.vc2010, "projectReferences", function(base, prj)
   local refs = premake.project.getdependencies(prj, 'linkOnly')
   if #refs > 0 then
      premake.push('<ItemGroup>')
      for _, ref in ipairs(refs) do
         local relpath = premake.vstudio.path(prj, premake.vstudio.projectfile(ref))
         premake.push('<ProjectReference Include=\"%s\">', relpath)
         premake.callArray(premake.vstudio.vc2010.elements.projectReferences, prj, ref)
         premake.vstudio.vc2010.element("UseLibraryDependencyInputs", nil, "true")
         premake.pop('</ProjectReference>')
      end
      premake.pop('</ItemGroup>')
   end
end)

root = repo_build.root

targetName = repo_build.target_name()
targetDir = repo_build.target_dir()

workspaceDir = repo_build.workspace_dir()

platform = repo_build.platform()

hostDepsDir = repo_build.host_deps_dir()
targetDepsDir = repo_build.target_deps_dir()
usdDepsDir = root.."/_build/usd-deps"

usdDir = usdDepsDir.."/usd/%{cfg.buildcfg}"
omniClientDir = targetDepsDir.."/omni_client_library/%{cfg.buildcfg}"

houdiniBuild = false
blenderBuild = false
bentleyBuild = false
isotropixBuild = false
mayaBuild = false
maxBuild = false
maxonBuild = false
nvUsdBuild = true
pxrUsdBuild = false
usdRoot = nil

function get_usd_version_info()
    local usd_flavor = os.getenv("OMNI_USD_FLAVOR")
    if _OPTIONS["usd-flavor"] ~= nil then
        usd_flavor = _OPTIONS["usd-flavor"]
    end

    local usd_ver = os.getenv("OMNI_USD_VER")
    if _OPTIONS["usd-ver"] ~= nil then
        usd_ver = _OPTIONS["usd-ver"]
    end

    local python_ver = os.getenv("OMNI_PYTHON_VER")
    if _OPTIONS["python-ver"] ~= nil then
        python_ver = _OPTIONS["python-ver"]
    end

    local local_path = nil
    if _OPTIONS["local-path"] ~= nil then
        local_path = _OPTIONS["local-path"]
    end
    return usd_flavor, usd_ver, python_ver, local_path
end

usdFlavor, usdVersion, pythonVer, local_path = get_usd_version_info()
if usdVersion ~= nil then
    local nodot, _ = usdVersion:gsub("%.", "")
    usdVer = tonumber(nodot)
end

if pythonVer ~= nil then
    pythonVerNoDot = pythonVer:gsub("%.", "")
end

if usdFlavor then
    houdiniBuild = usdFlavor == "houdini"
    blenderBuild = usdFlavor == "blender"
    bentleyBuild = usdFlavor == "bentley"
    isotropixBuild = usdFlavor == "isotropix"
    mayaBuild = usdFlavor == "maya"
    maxBuild = usdFlavor == "3dsmax"
    maxonBuild = usdFlavor == "maxon"
    nvUsdBuild = usdFlavor == "nv-usd"
    pxrUsdBuild = usdFlavor == "usd"
end

usdLibs = {
    "ar",
    "arch",
    "gf",
    "js",
    "kind",
    "pcp",
    "plug",
    "sdf",
    "tf",
    "trace",
    "usd",
    "usdGeom",
    "vt",
    "work"
}
usdOptions = {
    usd_root = usdDir,
    usd_lib_prefix = nil,
    usd_suppress_warnings = true,
    python_root = nil,
    python_version = nil,
    boost_python_prefix = nil
}

function merge_libs(common_libs, additional_libs)
    if additional_libs == nil then
        return common_libs
    end

    local result = {}
    for i=1,#common_libs do
        result[i] = common_libs[i]
    end
    for i=1,#additional_libs do
        result[#result + 1] = additional_libs[i]
    end

    return result
end

function use_carb()
    local carbSDKPath = targetDepsDir.."/carb_sdk_plugins"
    local carbSDKInclude = carbSDKPath.."/include"
    local carbSDKLibs = carbSDKPath.."/_build/"..platform.."/%{cfg.buildcfg}"

    externalincludedirs { carbSDKInclude }
    libdirs { carbSDKLibs }
    links { "carb" }

    filter { "configurations:debug" }
        defines { "CARB_DEBUG=1" }
    filter  { "configurations:release" }
        defines { "CARB_DEBUG=0" }
    filter {}
end

function use_client_library()
    externalincludedirs {
        targetDepsDir.."/omni_client_library/include"
    }

    libdirs { omniClientDir }
    links{ "omniclient" }
end

function use_pybind()
    externalincludedirs { targetDepsDir.."/pybind11/include" }
end

function use_usd(additionalUsdLibs)
    defines { "ENABLE_USD_TRACE" }

    -- Setup configurations that apply to all platforms
    filter { "configurations:debug" }
        -- Houdini USD doesn't ship with tbb_debug. We can only link to regular tbb lib.
        if houdiniBuild then
            links { "tbb" }
        elseif bentleyBuild then
            links { "tbbe" }
        elseif mayaBuild then
            links { "tbb" }
        else
            links { "tbb_debug" }
        end
    filter { "configurations:release" }
        if bentleyBuild then
            links { "tbbe" }
        else
            links { "tbb" }
        end
    filter {}

    if houdiniBuild then
        defines { "TBB_SUPPRESS_DEPRECATED_MESSAGES" }
    elseif blenderBuild then
        -- Suppress deprecated tbb/atomic.h and tbb/task.h warnings due to
        -- Blender's newer TBB libraries.
        defines { "TBB_SUPPRESS_DEPRECATED_MESSAGES" }
    elseif bentleyBuild then
        -- Suppress deprecated tbb/atomic.h and tbb/task.h warnings due to
        -- Bentley's newer TBB libraries.
        defines { "TBB_SUPPRESS_DEPRECATED_MESSAGES" }
    elseif mayaBuild then
        -- Suppress deprecated tbb/atomic.h and tbb/task.h warnings due to
        -- Maya's newer TBB libraries.
        defines { "TBB_SUPPRESS_DEPRECATED_MESSAGES" }
    end

    -- Setup platform-specific configurations
    filter { "system:windows" }
        -- Windows configuration
        if houdiniBuild then
            -- To avoid missing hboost_python37-vc141-mt-gd-x64-1_72.lib error.
            defines { "HBOOST_PYTHON_NO_LIB" }
            -- To avoid missing tbb_debug.lib error.
            filter { "configurations:debug" }
                defines { "__TBB_NO_IMPLICIT_LINKAGE=1" }               
        elseif mayaBuild then
            -- To avoid missing tbb_debug.lib error.
            filter { "configurations:debug" }
                defines { "__TBB_NO_IMPLICIT_LINKAGE=1" }
        elseif bentleyBuild then
            -- To avoid missing tbb_debug.lib error.
            filter { "configurations:debug" }
                defines { "__TBB_NO_IMPLICIT_LINKAGE=1" }
        end
    filter {}

    filter { "system:linux" }
        -- Linux configurations
        disablewarnings { "error=cpp", "error=unknown-pragmas", "error=undef" }
        linkoptions { "-Wl,-rpath="..usdDir.."/lib" }

    filter {}

    -- Setup python / boost_python options for repo_usd
    if pythonVerNoDot ~= "0" then
        usdOptions.python_root = usdDepsDir.."/python"
        usdOptions.python_version = pythonVer

        local msvcToolset = "vc141"
        if pythonVerNoDot == "310" then
            if pxrUsdBuild then
                if usdVer > 2208 then
                    msvcToolset = "vc142"
                end
            else
                msvcToolset = "vc142"
            end
            -- 3dsmax 2024 with python 310 needs vc141
            if maxBuild then
                msvcToolset = "vc141"
            end
        end
        if pythonVerNoDot == "311" then
            msvcToolset = "vc142"

            -- maya 2025
            if mayaBuild then
                msvcToolset = "vc143"
            end
        end

        if os.target() == "windows" then
            usdOptions.python_lib_name = "python"..pythonVerNoDot..".lib"
            usdOptions.python_lib_path = usdDepsDir.."/python/libs"
            usdOptions.python_include_path = usdDepsDir.."/python/include"

            if houdiniBuild then
                usdOptions.boost_python_prefix = "hboost_python"..pythonVerNoDot.."-mt"
            elseif bentleyBuild then
                usdOptions.boost_python_prefix = "boost_python"..pythonVerNoDot.."-mt"
            elseif blenderBuild then
                usdOptions.boost_python_prefix = {
                    release = "boost_python"..pythonVerNoDot.."-vc142-mt-x64",
                    debug = "boost_python"..pythonVerNoDot.."-vc142-mt-gyd-x64"
                }
            else
                usdOptions.boost_python_prefix = "boost_python"..pythonVerNoDot.."-"..msvcToolset.."-mt"
            end
        else
            usdOptions.python_lib_name = pylibs[pythonVerNoDot]
            usdOptions.python_lib_path = usdDepsDir.."/python/lib"
            usdOptions.python_include_path = usdDepsDir.."/python/include/"..pylibs[pythonVerNoDot]

            if houdiniBuild then
                usdOptions.boost_python_prefix = "libhboost_python"..pythonVerNoDot.."-mt"
            else
                usdOptions.boost_python_prefix = "libboost_python"..pythonVerNoDot
            end
        end
    end

    -- Gather all the USD libraries that need to be linked against
    -- First see if we are using a monolithic build of USD
    local mergedUsdLibs = {}
    local monolithicUsdLib, _ = repo_usd.find_usd_monolithic(usdOptions)
    if monolithicUsdLib ~= nil then
        -- We have a monolithic build of USD so we do not need to explicitly link
        -- all the different USD libraries
        mergedUsdLibs = { monolithicUsdLib }

        -- Since we already found the monolithic library we don't
        -- need to prepend or look for a prefix
        usdOptions.usd_lib_prefix = ""
    else
        mergedUsdLibs = merge_libs(usdLibs, additionalUsdLibs)
    end

    repo_usd.use_usd(usdOptions, mergedUsdLibs)

    -- Houdini 21 USD 25.05 requires explicit pxr_boost and pxr_python linking
    filter { "system:windows" }
        libdirs { "C:/Program Files/Side Effects Software/Houdini 21.0.631/custom/houdini/dsolib" }
        links { "libpxr_boost", "libpxr_python", "libpxr_ar" }
    filter {}
end

-- premake5.lua
workspace "OmniUsdResolver"

    -- add default arguments for the workspace
    local args = {
        windows_x86_64_enabled = true,
        copy_windows_debug_libs = false,
        linux_x86_64_enabled = true,
        linux_aarch64_enabled = true,
        allow_undefined_symbols_linux = false,
        macos_universal_enabled = false,
        extra_warnings = true,
        security_hardening = false,
        use_pch = false
    }

    repo_build.setup_workspace({
        windows_x86_64_enabled = args.windows_x86_64_enabled,
        linux_x86_64_enabled = args.linux_x86_64_enabled,
        linux_aarch64_enabled = args.linux_aarch64_enabled,
        macos_universal_enabled = args.macos_universal_enabled,
        copy_windows_debug_libs = args.copy_windows_debug_libs,
        allow_undefined_symbols_linux = args.allow_undefined_symbols_linux,
        extra_warnings = args.extra_warnings,
        security_hardening = args.security_hardening,
        use_pch = args.use_pch,
        fix_cpp_version = true,
    })

    -- This is defined in repo_build and repo_usd, remove it here
    -- so repo_usd can properly set it based on how USD was built
    removedefines { "_GLIBCXX_USE_CXX11_ABI*" }

    exceptionhandling "On"
    rtti "On"
    cppdialect "C++17"
    includedirs {
        "include",
        "source"
    }

    defines { "OMNIUSDRESOLVER_PYVER="..pythonVerNoDot }

    externalincludedirs { targetDepsDir }
    externalincludedirs { targetDepsDir.."/omni-trace/include" }
    externalincludedirs { targetDepsDir.."/omni-config-cpp/include" }

    use_carb()
    use_client_library()

    filter { "system:windows" }
        
        disablewarnings {
            "4003",
            "4100", -- unreferenced formal parameter
            "4127", -- conditional expression is constant
            "4189", -- 'x': local variable is initialized but not referenced
            "4201", -- nonstandard extension used: nameless struct/union
            "4456", -- declaration of 'x' hides previous local declaration (this happens a lot with OMNI_TRACE_SCOPE)
            "4506", -- no definition for inline function (this happens with Pixar headers)
            "4003", -- Suppress 'not enough arguments for function-like macro invocation 'BOOST_PP_SEQ_DETAIL_IS_NOT_EMPTY' https://github.com/PixarAnimationStudios/OpenUSD/issues/2624
        }

    filter { "system:linux" }
        buildoptions { "-fvisibility=hidden" }
        -- enforces RPATH instead of RUNPATH, needed for ubuntu version > 16.04
        linkoptions { "-Wl,--disable-new-dtags",
                      "-Wl,-rpath,'$$ORIGIN' -Wl,--export-dynamic" }
        disablewarnings {
            "unused-variable",
            "unused-parameter",
            "switch",
            "unused-but-set-variable",
            "unused-result",
            "deprecated",
            "deprecated-declarations",
            "unknown-pragmas",
            "multichar",
            "noexcept-type"
        }
        links { "stdc++fs" }
        links { "dl" }
        links { "util" }

    filter {}

    filter { "configurations:debug" }
        defines { "DEBUG_OUTPUT=1" }
    filter  { "configurations:release" }
        defines { "DEBUG_OUTPUT=0" }
    filter {}

group "Libraries"

project "omni_usd_resolver"
    kind "SharedLib"
    defines { "OMNIUSDRESOLVER_EXPORTS" }
    location (workspaceDir.."/%{prj.name}")
    includedirs {
        "include/"
    }
    files {
        "include/*.*",
        "source/library/*.*",
        "source/utils/*.*",
    }
    vpaths {
        ["utils/*"] = "source/utils/",
        ["*"] = ""
    }

    filter { "system:linux" }
        buildoptions { "-fPIC" }
        links { "rt" }
        removefiles { "source/library/version.rc" }
    filter { "system:windows" }
        defines { 
            "ARCH_OS_WINDOWS",
            "MFB_ALT_PACKAGE_NAME=omni_usd_resolver"
        }
    filter{}

    use_usd()

    copy_to_file("source/library/plugInfo-%{cfg.system}.json", "%{cfg.targetdir}/usd/omniverse/resolver/resources/plugInfo.json")

group "Python"
    if pythonVerNoDot ~= "0" then
        pyfolder = usdDepsDir.."/python"
        project("python"..pythonVerNoDot.."_omni_usd_resolver_bindings")
            location (workspaceDir.."/%{prj.name}")
            defines { "MODULE_NAME=omni_usd_resolver" }
            files {
                "source/bindings/python/omni_usd_resolver/*.*"
            }
            vpaths {
                ["*"] = ""
            }
            targetdir (targetDir.."/bindings-python/omni/usd_resolver")
            links { "omni_usd_resolver" }

            use_pybind()

            filter { "system:linux" }
                linkoptions{"-Wl,-rpath='$$ORIGIN/../../../bin'"}
            filter {}

            repo_build.define_bindings_python("_omni_usd_resolver", pyfolder, pythonVer)
            
            copy_to_file("source/tests/python/test_client.py", "%{cfg.targetdir}/test_client.py")
    end


group "Tests"
    if pythonVerNoDot ~= "0" then
        project("test_python")
            location (workspaceDir.."/%{prj.name}")
            kind "Utility"
            files {
                "source/tests/python/**.*",
            }
            vpaths {
                ["*"] = ""
            }
            filter { "system:windows" }
                usd_libdir = usdDir.."/lib"
                usd_bindir = usdDir.."/bin"
                debugenvs { "PATH=%PATH%;"..usd_libdir..";"..usd_bindir..";"..omniClientDir..";"..targetDir..";"..usdDepsDir.."/python" }
                debugenvs { "PYTHONPATH=%PYTHONPATH%;"..usd_libdir.."/python;"..omniClientDir.."/bindings-python;"..targetDir.."/bindings-python" }
                debugenvs { "PXR_PLUGINPATH_NAME="..targetDir.."/usd/omniverse/resources" }
                debugcommand(usdDepsDir.."/python/python.exe")
                debugargs { "tests/python/test_client.py" }
                debugdir( root )
            filter {}
    end

    project "omni_test_file_format"
        kind "SharedLib"
        defines { "OMNIUSDRESOLVER_EXPORTS" }
        location (workspaceDir.."/%{prj.name}")
        includedirs {
            "source/tests/shared/"
        }
        files {
            "source/tests/test_file_format/*.cpp",
            "source/tests/shared/*.*"
        }
        vpaths {
            ["tests/shared/*"] = "source/tests/shared/",
            ["*"] = ""
        }

        filter { "system:linux" }
            buildoptions { "-fPIC" }
            links { "rt" }
        filter{}

        use_usd()

        links{ "omni_usd_resolver" }

        copy_to_file("source/tests/test_file_format/plugInfo-%{cfg.system}.json", "%{cfg.targetdir}/test/fileformat/resources/plugInfo.json")

    project "omni_test_fallback"
        kind "SharedLib"
        defines { "OMNIUSDRESOLVER_EXPORTS" }
        location (workspaceDir.."/%{prj.name}")
        includedirs {
            "source/tests/shared/",
            "source/tests/test_fallback_plugin/"
        }
        files {
            "source/tests/test_fallback_plugin/TestPrimaryResolver_Ar2.cpp",
            "source/tests/test_fallback_plugin/TestSchemeResolver_Ar2.cpp",
            "source/tests/shared/*.*"
        }
        vpaths {
            ["tests/shared/*"] = "source/tests/shared/",
            ["*"] = ""
        }

        filter { "system:linux" }
            buildoptions { "-fPIC" }
            links { "rt" }
        filter{}

        use_usd()

        copy_to_file("source/tests/test_fallback_plugin/plugInfo-%{cfg.system}.json", "%{cfg.targetdir}/test/fallback/resources/plugInfo.json")
        copy_to_file("source/tests/test_fallback_plugin/plugInfo-%{cfg.system}-redist.json", "%{cfg.targetdir}/test/redist/resources/plugInfo.json")

    function test(prj_name, usd_libs)
        project(prj_name:gsub('/', '_'))
            location (workspaceDir.."/%{prj.name}")
            kind "ConsoleApp"
            language "C++"
            includedirs {
                "source/tests/shared/",
            }
            externalincludedirs {
                targetDepsDir.."/omni_client_library/include"
            }

            files {
                "source/tests/"..prj_name.."/**.*",
                "source/tests/shared/*.*"
            }
            vpaths {
                ["tests/shared/*"] = "source/tests/shared/",
                ["*"] = ""
            }

            links{ "omni_usd_resolver" }

            filter { "system:linux" }
                buildoptions { "-pthread" }
                links { "pthread" }
            filter {}

            use_usd(usd_libs)

            filter { "system:windows" }
                usd_libdir = usdDir.."/lib"
                usd_bindir = usdDir.."/bin"
                ot_bindir  = targetDepsDir.."/omni-trace/bin"
                debugenvs { "OMNI_TRACE_LIB="..ot_bindir.."/carb.omnitrace.plugin.dll" }
                debugenvs { "PATH=%PATH%;"..usd_libdir..";"..usd_bindir..";"..omniClientDir..";"..usdDepsDir.."/python" }
                debugdir( root.."/test-data/" )
            filter {}
    end

    function test_repro(prj_name, usd_libs)
        test("test_repro/"..prj_name, usd_libs)
        files {
            "source/tests/test_repro/"..prj_name..".*"
        }
        vpaths {
            ["*"] = ""
        }
    end

    test_repro("OM-27494")
    test_repro("OM-27691")
    test_repro("OM-27691-bis")
    test_repro("OM-49309")
    test_repro("OM-47199", {"ndr", "sdr", "usdShade"})
    test_repro("OM-47199-off", {"ndr", "sdr", "usdShade"})

    test("test_resolver", {"usdShade"})
    test("test_fallback")
