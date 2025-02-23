Rule = {}

function CreateRule(inRule)
    table.insert(Rule, inRule)
end

-- ███████╗██╗  ██╗ █████╗ ██████╗ ███████╗██████╗ ███████╗
-- ██╔════╝██║  ██║██╔══██╗██╔══██╗██╔════╝██╔══██╗██╔════╝
-- ███████╗███████║███████║██║  ██║█████╗  ██████╔╝███████╗
-- ╚════██║██╔══██║██╔══██║██║  ██║██╔══╝  ██╔══██╗╚════██║
-- ███████║██║  ██║██║  ██║██████╔╝███████╗██║  ██║███████║
-- ╚══════╝╚═╝  ╚═╝╚═╝  ╚═╝╚═════╝ ╚══════╝╚═╝  ╚═╝╚══════╝
--

function CreateShaderRule(inRuleName, inProfile, entryPoint, flags, inInputPath)
    local rule =
    {
        Name = inRuleName,
        Version = 2,
        OutputPaths = { '{ Repo:Bin }CompiledShaders/DIRECT3D12/{ File }' },
        CommandLine = '{ Repo:Tools }DirectXShaderCompiler/dxc.exe "{ Repo:Shaders }{ Path }" -Fo "{ Repo:Bin }CompiledShaders/DIRECT3D12/{ File }" -E ' .. entryPoint .. ' -HV 2021 -WX -O0 -Zi -Qembed_debug ' .. flags .. ' -T ' .. inProfile,
        InputFilters = { { Repo = "Shaders", PathPattern = inInputPath } },
        DepFile = { Path = '{ Repo:Intermediate }{ Dir }{ File }.d', Format = 'Make' },
        DepFileCommandLine = '{ Repo:Tools }DirectXShaderCompiler/dxc.exe "{ Repo:Shaders }{ Path }" -MF { Repo:Intermediate }{ Dir }{ File }.d -T ' .. inProfile,
    }
    table.insert(Rule, rule)
end

CreateShaderRule("Shader VS", "vs_6_8", "main", "-Qstrip_rootsignature", "*.vert.hlsl")
CreateShaderRule("Shader PS", "ps_6_8", "main", "-Qstrip_rootsignature", "*.pixel.hlsl")
CreateShaderRule("Shader CS", "cs_6_8", "main", "-Qstrip_rootsignature", "*.comp.hlsl")
CreateShaderRule("Root Signature", "rootsig_1_1", "DefaultRootSignature", "", "*.rs.hlsl")

-- ████████╗███████╗██╗  ██╗████████╗██╗   ██╗██████╗ ███████╗███████╗
-- ╚══██╔══╝██╔════╝╚██╗██╔╝╚══██╔══╝██║   ██║██╔══██╗██╔════╝██╔════╝
--    ██║   █████╗   ╚███╔╝    ██║   ██║   ██║██████╔╝█████╗  ███████╗
--    ██║   ██╔══╝   ██╔██╗    ██║   ██║   ██║██╔══██╗██╔══╝  ╚════██║
--    ██║   ███████╗██╔╝ ██╗   ██║   ╚██████╔╝██║  ██║███████╗███████║
--    ╚═╝   ╚══════╝╚═╝  ╚═╝   ╚═╝    ╚═════╝ ╚═╝  ╚═╝╚══════╝╚══════╝
--

function CreateTextureRule(inRuleName, inFormat, inFlags, inInputNameSuffixes, inMipCount)
    -- Generate 8 mips by default.
    inMipCount = inMipCount or 8

    local rule =
    {
        Name = inRuleName,
        Version = 0,
        OutputPaths = { '{ Repo:Bin }{ Dir }{ File }.dds' },
        CommandLine = '{ Repo:Tools }texconv/texconv.exe -y -nologo -sepalpha -dx10 ' .. inFlags .. ' -m ' .. inMipCount .. ' -f ' .. inFormat .. ' -o "{ Repo:Bin }{ Dir_NoTrailingSlash }" "{ Repo:Source }{ Path }"',
        InputFilters = {},
    }

	for i, name_suffix in ipairs(inInputNameSuffixes) do
        table.insert(rule.InputFilters, { Repo = "Source", PathPattern = "*" .. name_suffix .. ".png" })
        table.insert(rule.InputFilters, { Repo = "Source", PathPattern = "*" .. name_suffix .. ".tga" })
        table.insert(rule.InputFilters, { Repo = "Source", PathPattern = "*" .. name_suffix .. ".jpg" })
    end

    table.insert(Rule, rule)
end

CreateTextureRule("Texture BC1 sRGB", "BC1_UNORM_SRGB", "-srgb",  { "_albedo", "_emissive" })
CreateTextureRule("Texture BC1",      "BC1_UNORM",      "-srgb",  { "_orm" })
CreateTextureRule("Texture BC5",      "BC5_UNORM",      "",       { "_normal" })