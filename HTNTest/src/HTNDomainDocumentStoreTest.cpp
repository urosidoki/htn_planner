// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "Domain/Tooling/HTNDomainDocumentStore.h"

#include "gtest/gtest.h"

#include <filesystem>
#include <string>

TEST(HTNDomainDocumentStoreTest, OpenUnsavedIncludeParticipatesInSemanticAnalysis)
{
    const std::filesystem::path Root =
        std::filesystem::temp_directory_path() / "htn_document_store_semantic_test";
    const std::filesystem::path BasePath = Root / "base.domain";
    const std::filesystem::path MainPath = Root / "main.domain";

    const std::string BaseText =
        "(:domain Base base\n"
        "  (:method (do_combat ?inp_threat) base\n"
        "    (branch_base () ((!attack ?inp_threat)))\n"
        "  )\n"
        ")\n";

    const std::string MainText =
        "(:include \"base.domain\")\n"
        "(:domain Main top_level_domain\n"
        "  (:method (run ?inp_threat) top_level_method\n"
        "    (branch_main () ((Base::do_combat ?inp_threat)))\n"
        "  )\n"
        ")\n";

    HTNDomainDocumentStore Store;
    Store.OpenDocument(BasePath, BaseText, 1);
    Store.OpenDocument(MainPath, MainText, 1);

    const HTNCompilerToolingModel* Model = Store.GetToolingModel(MainPath);
    ASSERT_NE(Model, nullptr);

    const size_t CallOffset = MainText.find("Base::do_combat");
    ASSERT_NE(CallOffset, std::string::npos);
    EXPECT_EQ(
        Model->GetTokenKind(CallOffset),
        HTNCompilerToolingTokenKind::MethodValid);

    HTNCompilerToolingDefinition Definition;
    ASSERT_TRUE(Model->GetDefinitionAtOffset(CallOffset, Definition));
    EXPECT_EQ(Definition.FilePath.filename(), "base.domain");
}

TEST(HTNDomainDocumentStoreTest, ChangingIncludedBufferInvalidatesDependentSemanticModel)
{
    const std::filesystem::path Root =
        std::filesystem::temp_directory_path() / "htn_document_store_invalidation_test";
    const std::filesystem::path BasePath = Root / "base.domain";
    const std::filesystem::path MainPath = Root / "main.domain";

    const std::string BaseV1 =
        "(:domain Base base\n"
        "  (:method (do_combat ?inp_threat) base\n"
        "    (branch_base () ((!attack ?inp_threat)))\n"
        "  )\n"
        ")\n";

    const std::string BaseV2 =
        "(:domain Base base\n"
        "  (:method (renamed_combat ?inp_threat) base\n"
        "    (branch_base () ((!attack ?inp_threat)))\n"
        "  )\n"
        ")\n";

    const std::string MainText =
        "(:include \"base.domain\")\n"
        "(:domain Main top_level_domain\n"
        "  (:method (run ?inp_threat) top_level_method\n"
        "    (branch_main () ((Base::do_combat ?inp_threat)))\n"
        "  )\n"
        ")\n";

    HTNDomainDocumentStore Store;
    Store.OpenDocument(BasePath, BaseV1, 1);
    Store.OpenDocument(MainPath, MainText, 1);

    const size_t CallOffset = MainText.find("Base::do_combat");
    ASSERT_NE(CallOffset, std::string::npos);

    const HTNCompilerToolingModel* FirstModel =
        Store.GetToolingModel(MainPath);
    ASSERT_NE(FirstModel, nullptr);
    EXPECT_EQ(
        FirstModel->GetTokenKind(CallOffset),
        HTNCompilerToolingTokenKind::MethodValid);

    const std::uint64_t PreviousGeneration = Store.GetGeneration();
    ASSERT_TRUE(Store.UpdateDocument(BasePath, BaseV2, 2));
    EXPECT_GT(Store.GetGeneration(), PreviousGeneration);

    const HTNCompilerToolingModel* SecondModel =
        Store.GetToolingModel(MainPath);
    ASSERT_NE(SecondModel, nullptr);
    EXPECT_EQ(
        SecondModel->GetTokenKind(CallOffset),
        HTNCompilerToolingTokenKind::MethodInvalid);

    std::string SourceText;
    ASSERT_TRUE(Store.Read(BasePath, SourceText));
    EXPECT_EQ(SourceText, BaseV2);
}

TEST(HTNDomainDocumentStoreTest, ResolvesOverloadedMethodDefinitionsByCallArity)
{
    const auto Path = std::filesystem::temp_directory_path() / "overload_tooling.domain";
    const std::string Source =
        "(:domain Overloads top_level_domain\n"
        " (:method (work) (b () ((!zero))))\n"
        " (:method (work ?inp_x) (b () ((!one ?inp_x))))\n"
        " (:method (run) top_level_method (b () ((work) (work (+ 1 2)) (&work 3) (Overloads::work 4))))\n"
        ")";
    HTNDomainDocumentStore Store;
    Store.OpenDocument(Path, Source, 1);
    const auto* Model = Store.GetToolingModel(Path);
    ASSERT_NE(Model, nullptr);
    EXPECT_TRUE(Model->GetDiagnostics().empty());
    const char* Calls[] = {"(work)", "(work (+", "(&work", "(Overloads::work"};
    const int Lines[] = {2, 3, 3, 3};
    for (size_t I = 0; I < 4; ++I)
    {
        const size_t Offset = Source.find(Calls[I], Source.find("(:method (run)")) + 1;
        HTNCompilerToolingDefinition Definition;
        ASSERT_TRUE(Model->GetDefinitionAtOffset(Offset, Definition));
        EXPECT_EQ(Definition.Range.Begin.Line, Lines[I]);
    }
}

TEST(HTNDomainDocumentStoreTest, ResolvesAxiomOverloadsInsideMethodsAndAxioms)
{
    const auto Path = std::filesystem::temp_directory_path() / "axiom_overload_tooling.domain";
    const std::string Source =
        "(:domain Overloads top_level_domain\n"
        " (:method (run) top_level_method (b (and (#work) (#work 1) (#Overloads::work 2)) ()))\n"
        " (:axiom (work) (and (#work 1)))\n"
        " (:axiom (work ?inp_x) ())\n"
        ")";
    HTNDomainDocumentStore Store;
    Store.OpenDocument(Path, Source, 1);
    const auto* Model = Store.GetToolingModel(Path);
    ASSERT_NE(Model, nullptr);
    EXPECT_TRUE(Model->GetDiagnostics().empty());
    const size_t Offsets[] = {Source.find("#work)"), Source.find("#work 1"),
                              Source.find("#Overloads::work"), Source.rfind("#work 1")};
    const int Lines[] = {3, 4, 4, 4};
    for (size_t I = 0; I < 4; ++I)
    {
        HTNCompilerToolingDefinition Definition;
        ASSERT_TRUE(Model->GetDefinitionAtOffset(Offsets[I], Definition));
        EXPECT_EQ(Definition.Range.Begin.Line, Lines[I]);
    }
}
