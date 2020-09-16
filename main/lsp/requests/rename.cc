#include "main/lsp/requests/rename.h"
#include "absl/strings/match.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "core/lsp/QueryResponse.h"
#include "main/lsp/ShowOperation.h"
#include "main/lsp/json_types.h"
#include "main/lsp/lsp.h"

using namespace std;

namespace sorbet::realmain::lsp {

std::optional<std::string> validateName(std::string_view name) {
    // Name must be a valid Ruby identifier.
    // TODO: Validate.
    return nullopt;
}

unique_ptr<WorkspaceEdit> RenameTask::getRenameEdits(const core::GlobalState &gs, core::SymbolRef symbol,
                                                     std::string_view newName) {
    vector<unique_ptr<Location>> references;
    // tie(gs, references) = getReferencesToSymbol(move(gs), symbol, move(references));

    auto originalName = symbol.data(gs)->name.toString(gs);
    auto we = make_unique<WorkspaceEdit>();
    // TextEdit
    UnorderedMap<string, vector<unique_ptr<TextEdit>>> edits;
    for (auto &location : references) {
        // Get text at location.
        // TODO: Not payload files...?
        auto fref = config.uri2FileRef(gs, location->uri);
        if (fref.data(gs).isPayload()) {
            // We don't support renaming things in payload files.
            // TODO: Error?
            continue;
        }

        auto loc = location->range->toLoc(gs, fref);
        auto source = loc.source(gs);
        std::vector<std::string> strs = absl::StrSplit(source, "::");
        strs[strs.size() - 1] = string(newName);
        edits[location->uri].push_back(make_unique<TextEdit>(move(location->range), absl::StrJoin(strs, "::")));
    }

    vector<unique_ptr<TextDocumentEdit>> textDocEdits;
    for (auto &item : edits) {
        // TODO: Version.
        textDocEdits.push_back(make_unique<TextDocumentEdit>(
            make_unique<VersionedTextDocumentIdentifier>(item.first, JSONNullObject()), move(item.second)));
    }
    we->documentChanges = move(textDocEdits);

    return we;
}

unique_ptr<ResponseMessage> RenameTask::runRequest(LSPTypecheckerDelegate &typechecker) {
    const core::GlobalState &gs = typechecker.state();

    auto response = make_unique<ResponseMessage>("2.0", id, LSPMethod::TextDocumentRename);
    if (!config.opts.lspRenameEnabled) {
        response->error = make_unique<ResponseError>(
            (int)LSPErrorCodes::InvalidRequest, "The `Rename` LSP feature is experimental and disabled by default.");
        return response;
    }

    prodCategoryCounterInc("lsp.messages.processed", "textDocument.rename");
    // Sanity check the text.
    if (auto validationError = validateName(params->newName)) {
        response->error = make_unique<ResponseError>((int)LSPErrorCodes::InvalidRequest, *validationError);
        return response;
    }

    ShowOperation op(config, "References", "Renaming...");

    auto result = queryByLoc(typechecker, params->textDocument->uri, *params->position, LSPMethod::TextDocumentRename);
    if (result.error) {
        // An error happened while setting up the query.
        response->error = move(result.error);
    } else {
        // An explicit null indicates that we don't support this request (or that nothing was at the location).
        // Note: Need to correctly type variant here so it goes into right 'slot' of result variant.
        response->result = variant<JSONNullObject, unique_ptr<WorkspaceEdit>>(JSONNullObject());
        auto &queryResponses = result.responses;
        if (!queryResponses.empty()) {
            auto resp = move(queryResponses[0]);
            // Only supports rename requests from constants and class definitions.
            if (auto constResp = resp->isConstant()) {
                response->result = getRenameEdits(gs, constResp->symbol, params->newName);
            } else if (auto defResp = resp->isDefinition()) {
                if (defResp->symbol.data(gs)->isClassOrModule()) {
                    response->result = getRenameEdits(gs, defResp->symbol, params->newName);
                }
            }
        }
    }

    return response;
}

} // namespace sorbet::realmain::lsp
