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
        Version = 1,
        OutputPaths = { '{ Repo:Bin }CompiledShaders/DIRECT3D12/{ File }' },
        CommandLine = '{ Repo:Tools }DirectXShaderCompiler/dxc.exe "{ Repo:Shaders }{ Path }" -Fo "{ Repo:Bin }CompiledShaders/DIRECT3D12/{ File }" -E ' .. entryPoint .. ' -HV 2021 -WX -O0 -Zi -Qembed_debug ' .. flags .. ' -T ' .. inProfile,
        InputFilters = { { Repo = "Shaders", PathPattern = inInputPath } },
        DepFile = { Path = '{ Repo:Intermediate }{ Dir }{ File }.d', Format = 'Make' },
        DepFileCommandLine = '{ Repo:Tools }DirectXShaderCompiler/dxc.exe "{ Repo:Shaders }{ Path }" -MF { Repo:Intermediate }{ Dir }{ File }.d -T ' .. inProfile,
    }
    table.insert(Rule, rule)
end

CreateShaderRule("Shader VS", "vs_6_4", "main", "-Qstrip_rootsignature", "*.vert.hlsl")
CreateShaderRule("Shader PS", "ps_6_4", "main", "-Qstrip_rootsignature", "*.pixel.hlsl")
CreateShaderRule("Shader CS", "cs_6_4", "main", "-Qstrip_rootsignature", "*.comp.hlsl")
CreateShaderRule("Root Signature", "rootsig_1_1", "DefaultRootSignature", "", "*.rs.hlsl")