// Copyright (c) 2026 Jose Antonio Escribano. All rights reserved.

#include "Translator/HTNCCodeGenerator.h"
#include "Translator/HTNCompilerDomainLoader.h"
#include "Domain/Diagnostics/HTNDiagnosticSink.h"

#include "gtest/gtest.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
class TempDomainSet
{
public:
    explicit TempDomainSet(const char* inName)
        : mDirectory(std::filesystem::temp_directory_path() / inName)
    {
        std::error_code Ec;
        std::filesystem::remove_all(mDirectory, Ec);
        std::filesystem::create_directories(mDirectory, Ec);
    }

    ~TempDomainSet()
    {
        std::error_code Ec;
        std::filesystem::remove_all(mDirectory, Ec);
    }

    void Write(const char* inFileName, const std::string& inText) const
    {
        std::ofstream File(mDirectory / inFileName, std::ios::binary);
        ASSERT_TRUE(File.good());
        File << inText;
    }

    std::filesystem::path Path(const char* inFileName) const
    {
        return mDirectory / inFileName;
    }

private:
    std::filesystem::path mDirectory;
};

std::string GenerateDomain(const std::filesystem::path& inSourcePath,
                           const std::filesystem::path& inOutputPath, std::string& outError)
{
    HTNCCodeGeneratorOptions Options;
    Options.OutputSourcePath = inOutputPath.string();
    Options.EntryPointName = "CreateOverrideRegressionHTN";
    Options.SourceFilePath = inSourcePath.string();
    HTNCompilerDomainLoader Loader;
    HTNCompilerDomainLoadResult Compiler;
    HTNDiagnosticSink Diagnostics;
    if (!Loader.Load(inSourcePath.string(), Compiler, Diagnostics))
    {
        const HTNDiagnostic* Diagnostic = Diagnostics.GetFirstError();
        outError = Diagnostic ? Diagnostic->Message : "Compiler domain load failed";
        return {};
    }
    HTNCCodeGenerator Generator;
    if (!Generator.Generate(Compiler.Domain, Options, outError))
        return {};

    std::ifstream Input(inOutputPath, std::ios::binary);
    if (!Input.good())
        return {};
    return std::string(std::istreambuf_iterator<char>(Input), std::istreambuf_iterator<char>());
}

} // namespace

// -----------------------------------------------------------------------------
// Method overrides
// -----------------------------------------------------------------------------







TEST(HTNMethodOverrideRegressionTest, QualifiedImplementationsSurviveCodeGeneration)
{
    TempDomainSet Domains("htn_method_override_codegen_qualified");
    Domains.Write("Base.domain", "(:domain Base base\n (:method (act ?inp_x) base (b () ((!base_action ?inp_x))))\n)\n");
    Domains.Write("Root.domain", "(:include \"Base.domain\")\n(:domain Root top_level_domain\n (:method (act ?inp_x) overrides Base (b () ((Base::act ?inp_x) (!root ?inp_x))))\n (:method (run) top_level_method (b () ((act 1))))\n)\n");
    std::string Error;
    const std::string Generated = GenerateDomain(Domains.Path("Root.domain"), Domains.Path("out.generated.c"), Error);
    ASSERT_FALSE(Generated.empty()) << Error;
    EXPECT_NE(Generated.find("Base::act"), std::string::npos);
    EXPECT_NE(Generated.find("Root::act"), std::string::npos);
}

// -----------------------------------------------------------------------------
// Axiom overrides
// -----------------------------------------------------------------------------






TEST(HTNAxiomOverrideRegressionTest, EmptyBaseImplementationCanBeCalledQualifiedFromOverride)
{
    TempDomainSet Domains("htn_axiom_override_empty_qualified_base");
    Domains.Write("Base.domain", "(:domain Base base\n (:axiom (ready ?inp_x) base ())\n)\n");
    Domains.Write("Root.domain", "(:include \"Base.domain\")\n(:domain Root top_level_domain\n (:axiom (ready ?inp_x) overrides Base (and (#Base::ready ?inp_x) (root_ready ?inp_x)))\n (:method (run) top_level_method (b (and (#ready enemy)) ((!ok))))\n)\n");
    std::string Error;
    const std::string Generated = GenerateDomain(Domains.Path("Root.domain"), Domains.Path("out.generated.c"), Error);
    ASSERT_FALSE(Generated.empty()) << Error;
    EXPECT_NE(Generated.find("Base::ready"), std::string::npos);
}

TEST(HTNAxiomOverrideRegressionTest, QualifiedAxiomInsideAndGeneratesShortCircuitFailurePath)
{
    TempDomainSet Domains("htn_axiom_override_and_short_circuit_regression");
    Domains.Write("Base.domain", "(:domain Base base\n (:axiom (ready ?inp_x) base (and (required ?inp_x)))\n)\n");
    Domains.Write("Root.domain", "(:include \"Base.domain\")\n(:domain Root top_level_domain\n (:axiom (test ?inp_x) (and (#Base::ready ?inp_x) (second_guard ?inp_x)))\n (:method (run) top_level_method (b (and (#test enemy)) ((!ok))))\n)\n");
    std::string Error;
    const std::string Generated = GenerateDomain(Domains.Path("Root.domain"), Domains.Path("out.generated.c"), Error);
    ASSERT_FALSE(Generated.empty()) << Error;
    const size_t BaseCall = Generated.find("Base::ready");
    ASSERT_NE(BaseCall, std::string::npos);
    EXPECT_NE(Generated.find("second_guard", BaseCall), std::string::npos);
}

TEST(HTNAxiomOverrideRegressionTest, QualifiedAxiomInsideNotGenerates)
{
    TempDomainSet Domains("htn_axiom_override_not_regression");
    Domains.Write("Base.domain", "(:domain Base base\n (:axiom (blocked ?inp_x) base (and (blocked_fact ?inp_x)))\n)\n");
    Domains.Write("Root.domain", "(:include \"Base.domain\")\n(:domain Root top_level_domain\n (:axiom (ready ?inp_x) (and (not (#Base::blocked ?inp_x))))\n (:method (run) top_level_method (b (and (#ready enemy)) ((!ok))))\n)\n");
    std::string Error;
    const std::string Generated = GenerateDomain(Domains.Path("Root.domain"), Domains.Path("out.generated.c"), Error);
    ASSERT_FALSE(Generated.empty()) << Error;
    EXPECT_NE(Generated.find("Base::blocked"), std::string::npos);
}

TEST(HTNAxiomOverrideRegressionTest, QualifiedParentChainGeneratesWithoutFallingBackToGenericEvaluator)
{
    TempDomainSet Domains("htn_axiom_override_parent_chain_codegen");
    Domains.Write("Base.domain", "(:domain Base base\n (:axiom (ready ?inp_x) base ())\n)\n");
    Domains.Write("Mid.domain", "(:include \"Base.domain\")\n(:domain Mid base\n (:axiom (ready ?inp_x) overrides Base (and (#Base::ready ?inp_x) (mid_ready ?inp_x)))\n)\n");
    Domains.Write("Root.domain", "(:include \"Mid.domain\")\n(:domain Root top_level_domain\n (:axiom (ready ?inp_x) overrides Mid (or (#Mid::ready ?inp_x) (root_fallback ?inp_x)))\n (:method (run) top_level_method (b (and (#ready enemy)) ((!ok))))\n)\n");
    std::string Error;
    const std::string Generated = GenerateDomain(Domains.Path("Root.domain"), Domains.Path("out.generated.c"), Error);
    ASSERT_FALSE(Generated.empty()) << Error;
    EXPECT_EQ(Error.find("generic condition evaluator"), std::string::npos);
    EXPECT_NE(Generated.find("Base::ready"), std::string::npos);
    EXPECT_NE(Generated.find("Mid::ready"), std::string::npos);
}

// -----------------------------------------------------------------------------
// Constant overrides
// -----------------------------------------------------------------------------







TEST(HTNConstantsOverrideRegressionTest, GeneratorContainsOnlyEffectiveValuesAfterPartialChain)
{
    TempDomainSet Domains("htn_constant_override_codegen_effective_regression");
    Domains.Write("Base.domain", "(:domain Base base\n (:constants C base (attack \"normal\") (speed \"slow\"))\n)\n");
    Domains.Write("Mid.domain", "(:include \"Base.domain\")\n(:domain Mid base\n (:constants C2 overrides Base (attack \"ninja\"))\n)\n");
    Domains.Write("Root.domain", "(:include \"Mid.domain\")\n(:domain Root top_level_domain\n (:constants C3 overrides Mid (attack \"elite\"))\n (:method (run) top_level_method (b () ((!use @attack @speed))))\n)\n");
    std::string Error;
    const std::string Generated = GenerateDomain(Domains.Path("Root.domain"), Domains.Path("out.generated.c"), Error);
    ASSERT_FALSE(Generated.empty()) << Error;
    EXPECT_NE(Generated.find("elite"), std::string::npos);
    EXPECT_NE(Generated.find("slow"), std::string::npos);
    EXPECT_EQ(Generated.find("\"normal\""), std::string::npos);
    EXPECT_EQ(Generated.find("\"ninja\""), std::string::npos);
}
