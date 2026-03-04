// SPDX-FileCopyrightText: Copyright (c) 2022-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: LicenseRef-NvidiaProprietary
//
// NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
// property and proprietary rights in and to this material, related
// documentation and any modifications thereto. Any use, reproduction,
// disclosure or distribution of this material and related documentation
// without an express license agreement from NVIDIA CORPORATION or
// its affiliates is strictly prohibited.

#include "OmniUsdResolver_Ar2.h"

#include "DebugCodes.h"
#include "MdlHelper.h"
#include "Notifications.h"
#include "OmniUsdAsset.h"
#include "OmniUsdResolverContext_Ar2.h"
#include "OmniUsdWritableAsset.h"
#include "ResolverHelper.h"
#include "UsdIncludes.h"
#include "utils/OmniClientUtils.h"
#include "utils/PathUtils.h"
#include "utils/StringUtils.h"
#include "utils/Time.h"
#include "utils/Trace.h"

#include <pxr/usd/ar/filesystemAsset.h>
#include <pxr/usd/ar/filesystemWritableAsset.h>

#include <OmniClient.h>

// Force import of Ar_ResolverFactoryBase vtable and type_info from libpxr_ar.dll.
// Required for dynamic_cast<Ar_ResolverFactoryBase*> across the DLL boundary to succeed
// in TfType::GetFactory<Ar_ResolverFactoryBase>(). Ar_ResolverFactory<T> is a header-only
// template instantiated locally; Ar_ResolverFactoryBase has AR_API so its identity comes
// from libpxr_ar.dll automatically.
#pragma comment(linker, "/INCLUDE:??1Ar_ResolverFactoryBase@pxrInternal_v0_25_5__pxrReserved__@@UEAA@XZ")

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{
inline bool _IsSearchPath(const std::string& assetPath)
{
    return isRelativePath(assetPath) && !isFileRelative(assetPath);
}
} // namespace

// AR_DEFINE_RESOLVER(OmniUsdResolver, ArResolver) is the standard USD macro but relies on
// the ARCH_CONSTRUCTOR / .pxrctor PE section mechanism to invoke the TF_REGISTRY_FUNCTION at
// DLL load. On Windows with Houdini 21 / USD 25.05 this section is not emitted in our build.
// Register the type and factory via a standard C++ static initialiser instead.
namespace {
struct _ResolverRegistration {
    _ResolverRegistration() {
        TfType::Define<OmniUsdResolver, TfType::Bases<ArResolver>>()
            .SetFactory<Ar_ResolverFactory<OmniUsdResolver>>();
    }
} _resolverRegistration;
}

OmniUsdResolver::OmniUsdResolver()
{
}

OmniUsdResolver::~OmniUsdResolver()
{
}

std::string OmniUsdResolver::_CreateIdentifier(const std::string& assetPath, const ArResolvedPath& anchorAssetPath) const
{
    if (assetPath.empty())
    {
        // nothing to do if we don't have an asset path to create an identifier for
        TF_DEBUG(OMNI_USD_RESOLVER).Msg("%s: empty assetPath\n", TF_FUNC_NAME().c_str());
        return assetPath;
    }

    std::string assetIdentifier;
    if (anchorAssetPath.empty() || isRelativePath(anchorAssetPath))
    {
        TF_DEBUG(OMNI_USD_RESOLVER)
            .Msg("%s: %s anchorAssetPath\n", TF_FUNC_NAME().c_str(), anchorAssetPath.empty() ? "empty" : "relative");

        // Without an explicit anchor there is not much we can do when creating an identifier
        // for the assetPath. If it is a file-relative path (e.g starts with ./ or ../) we will want to normalize it
        // using file path normalization from Tf. If it's anything else (omniverse URL) we will use normalization
        // from client-library
        //
        // Note: We are intentionally not using the base URL set on client-library here. If no base URL has been set,
        // e.g omniClientPushBaseURL, the current working directory would be used. This is not the expected
        // behavior for CreateIdentifier as we are trying to identify an assetPath that exists (SdfLayer::FindOrOpen).
        // For CreateIdentifierForNewAsset we would want to use the base URL as the expectation there is
        // to create an assetPath that does not exist (SdfLayer::CreateNew)
        // This behavior is modeled off ArDefaultResolver::CreateIdentifier and
        // ArDefaultResolver::CreateIdentifierForNewAsset
        assetIdentifier = isRelativePath(assetPath) ? TfNormPath(assetPath) : normalizeUrl(assetPath);
    }
    else if (mdl_helper::IsMdlIdentifier(assetPath))
    {
        // See OM-47199 / OM-57465
        // MDL asset paths are special in the sense that we do not want to apply the "look here first"
        // strategy. Doing so would result in calls to Nucleus that 99 percent of the time would fail.
        // There is no need to normalize the assetPath as it already matches one of the core MDL assets
        TF_DEBUG(OMNI_USD_RESOLVER_MDL).Msg("%s: %s is a core MDL asset\n", TF_FUNC_NAME().c_str(), assetPath.c_str());
        assetIdentifier = assetPath;
    }
    else
    {
        auto anchoredAssetPath =
            makeString(omniClientCombineUrls, anchorAssetPath.GetPathString().c_str(), assetPath.c_str());

        if (_IsSearchPath(assetPath) && Resolve(anchoredAssetPath).empty())
        {
            // Any other non-MDL search paths should use the "look here first" strategy, meaning that
            // we first try to resolve the anchored asset path. If the anchored asset path does not resolve
            // the search path will be returned as-is (so it can later be resolved by the configured search paths)
            TF_DEBUG(OMNI_USD_RESOLVER).Msg("%s: %s is a search path\n", TF_FUNC_NAME().c_str(), assetPath.c_str());
            assetIdentifier = normalizeUrl(assetPath);
        }

        if (assetIdentifier.empty())
        {
            assetIdentifier = std::move(anchoredAssetPath);
        }
    }

    TF_DEBUG(OMNI_USD_RESOLVER).Msg("%s: %s -> %s\n", TF_FUNC_NAME().c_str(), assetPath.c_str(), assetIdentifier.c_str());
    return assetIdentifier;
}

std::string OmniUsdResolver::_CreateIdentifierForNewAsset(const std::string& assetPath,
                                                          const ArResolvedPath& anchorAssetPath) const
{
    if (assetPath.empty())
    {
        TF_DEBUG(OMNI_USD_RESOLVER).Msg("%s: empty assetPath\n", TF_FUNC_NAME().c_str());
        return assetPath;
    }

    if (isRelativePath(assetPath))
    {
        static const std::string kDot{ "." };

        // If we have a relative path that we need to create a new asset for
        // a normalized anchor asset path is also required. An empty anchor asset path
        // will be expanded to whatever base URL is set in client-library (omniClientPushBaseURL).
        // In most cases this will be the current working directory but using the base URL from
        // client-library instead of getcwd() gives us the benefit of URL support
        const std::string anchor = anchorAssetPath.empty() || isRelativePath(anchorAssetPath.GetPathString()) ?
                                       makeString(omniClientCombineWithBaseUrl, kDot.c_str()) :
                                       anchorAssetPath.GetPathString();

        // When creating an identifier for a new asset, i.e SdfLayer::CreateNew, we want to use the same
        // logic as creating a normal identifier. The reason for separating _CreateIdentifier and
        // _CreateIdentifierForNewAsset is that it's new API defined in Ar 2.0.
        return _CreateIdentifier(assetPath, ArResolvedPath(anchor));
    }

    // We have an absolute asset path that just needs to be normalized
    std::string identifier = normalizeUrl(assetPath);

    TF_DEBUG(OMNI_USD_RESOLVER).Msg("%s: %s -> %s\n", TF_FUNC_NAME().c_str(), assetPath.c_str(), identifier.c_str());
    return identifier;
}

OmniUsdResolverCache::Entry OmniUsdResolver::_ResolveThroughCache(const std::string& identifier) const
{
    static constexpr std::string_view kSdfFormatArgs{ ":SDF_FORMAT_ARGS:" };
    const std::string identifierStripped = identifier.substr(0, identifier.find(kSdfFormatArgs));

    OmniUsdResolverCache::Entry cacheEntry;

    auto cache = m_threadCache.GetCurrentCache();
    if (!cache || !cache->Get(identifierStripped, cacheEntry))
    {
        cacheEntry.resolvedPath = ResolverHelper::Resolve(
            identifierStripped, cacheEntry.url, cacheEntry.version, cacheEntry.modifiedTime, cacheEntry.size);

        if (cache)
        {
            cache->Add(identifierStripped, cacheEntry);
        }
    }

    return cacheEntry;
}

ArResolvedPath OmniUsdResolver::_Resolve(const std::string& assetPath) const
{
    auto cacheEntry = _ResolveThroughCache(assetPath);

    TF_DEBUG(OMNI_USD_RESOLVER)
        .Msg("%s: %s -> %s\n", TF_FUNC_NAME().c_str(), assetPath.c_str(), cacheEntry.resolvedPath.c_str());

    return ArResolvedPath(std::move(cacheEntry.resolvedPath));
}
ArResolvedPath OmniUsdResolver::_ResolveForNewAsset(const std::string& assetPath) const
{
    // When resolving for a new asset there is nothing special to handle. Folders are created on-demand
    // and do not require calls such as mkdir. Normal file paths will have their directories created when
    // the asset is opened for writing.
    // We are intentionally not using the cache here since the layer has not been created yet.
    auto resolvedUrl = resolveUrl(assetPath);
    if (isLocal(resolvedUrl))
    {
        // Local files can be accessed directly
        return ArResolvedPath(fixLocalPath(safeString(resolvedUrl->path)));
    }

    std::string result = urlToString(*resolvedUrl);

    TF_DEBUG(OMNI_USD_RESOLVER).Msg("%s: %s -> %s\n", TF_FUNC_NAME().c_str(), assetPath.c_str(), result.c_str());
    return ArResolvedPath(std::move(result));
}
ArResolverContext OmniUsdResolver::_CreateDefaultContext() const
{
    TF_DEBUG(OMNI_USD_RESOLVER_CONTEXT).Msg("%s\n", TF_FUNC_NAME().c_str());
    // An entry-point to build a context when there might not be any information about the asset being loaded
    // In most cases this is the context used when a new stage is created and
    return { OmniUsdResolverContext() };
}
ArResolverContext OmniUsdResolver::_CreateDefaultContextForAsset(const std::string& assetPath) const
{
    // CreateDefaultContextForAsset is usually called towards the beginning of UsdStage::Open
    // with the resolved asset path / identifier (root layer). The context returned here will be bound and used
    // for resolves where the context needs to be inferred (i.e SdfAssetPath)
    // It's typically used for more global things like determining the Sequence / Shot / Model
    return { OmniUsdResolverContext(assetPath) };
}
ArResolverContext OmniUsdResolver::_CreateContextFromString(const std::string& contextStr) const
{
    // This is not common with most asset management systems but sometimes there is a need to load
    // the entire context from JSON or a file path to a JSON file (or XML, or YAML, etc.).
    // The separate function here makes it easier for implementations to know that the context is being hydrated
    // from something string-like and not make that determination in _CreateDefaultContextForAsset
    return {};
}
bool OmniUsdResolver::_IsContextDependentPath(const std::string& assetPath) const
{
    // As far as I know nothing is really a context-dependent path. A context-dependent path would
    // be something similar to an identifier hydrated with context-specific information. For example:
    // > omniverse://some/awesome/file.usd?checkpoint=latest
    // resolves to:
    // > omniverse://some/awesome/file.usd?checkpoint=5
    // since the query arg "checkpoint" could substitute "latest" with 5 from the context during resolve
    // This may, or may not, play more of a role with "Manifests"
    return false;
}
void OmniUsdResolver::_RefreshContext(const ArResolverContext& context)
{
    // There is nothing really to refresh for the OmniUsdResolverContext. If our context was populated
    // with information from Nucleus and cached locally, this would be the place to refresh that cache
}

void OmniUsdResolver::_BindContext(const ArResolverContext& context, VtValue* bindingData)
{
    static const std::string kEmpty;
    if (context.IsEmpty())
    {
        omniClientPushBaseUrl(kEmpty.c_str());
    }
    else
    {
        auto* ctx = context.Get<OmniUsdResolverContext>();
        if (!ctx)
        {
            OMNI_LOG_ERROR("Unknown resolver context object: %s", context.GetDebugString().c_str());
            omniClientPushBaseUrl(kEmpty.c_str());
        }
        else
        {
            TF_DEBUG(OMNI_USD_RESOLVER_CONTEXT).Msg("%s: Bound %s\n", TF_FUNC_NAME().c_str(), ctx->GetAssetPath().c_str());
            omniClientPushBaseUrl(ctx->GetAssetPath().c_str());
        }
    }
}
void OmniUsdResolver::_UnbindContext(const ArResolverContext& context, VtValue* bindingData)
{
    static const std::string kEmpty;
    if (context.IsEmpty())
    {
        omniClientPopBaseUrl(kEmpty.c_str());
    }
    else
    {
        auto* ctx = context.Get<OmniUsdResolverContext>();
        if (!ctx)
        {
            OMNI_LOG_ERROR("Unknown resolver context object: %s", context.GetDebugString().c_str());
            omniClientPopBaseUrl(kEmpty.c_str());
        }
        else
        {
            TF_DEBUG(OMNI_USD_RESOLVER_CONTEXT).Msg("%s: Unbound %s\n", TF_FUNC_NAME().c_str(), ctx->GetAssetPath().c_str());
            omniClientPopBaseUrl(ctx->GetAssetPath().c_str());
        }
    }
}

ArResolverContext OmniUsdResolver::_GetCurrentContext() const
{
    return { OmniUsdResolverContext(safeString(omniClientGetBaseUrl())) };
}

ArTimestamp OmniUsdResolver::_GetModificationTimestamp(const std::string& assetPath,
                                                       const ArResolvedPath& resolvedPath) const
{
    // _GetModificationTimestamp is used for calls like SdfLayer::Reload to determine if the asset needs to be reloaded.

    // _GetModificationTimestamp has changed from Ar 1 and returns a double over a VtValue.

    // This has a large impact on how layers are reloaded in Usd. For starters, precision of the timestamp
    // is important. If the backing asset system only guarantees second precision (i.e Nucleus) and a layer
    // is saved multiple times in less than a second it will not been seen as modified. This would result
    // in the layer not being reloaded. Our Ar 1 implementation used version instead of modTime to get around
    // the issue. In Ar 2 we can't do the same thing since we need to return a double and version is a string
    // which is not guaranteed to be a number. In most cases this is not an issue, it is an issue with unit tests
    // and usually requires a sleep of 1-2 seconds before calling SdfLayer::Save() in succession

    double timestamp{ 0 };

    auto cacheEntry = _ResolveThroughCache(assetPath);
    if (!cacheEntry.resolvedPath.empty())
    {
        // Only use the version string for omniverse URLs as they are usually monotonically increasing
        // Other providers such as S3 will return an etag (similar to a hash) in which case using the modTime
        // is preferred. For local files we will also want to use modTime as they don't support version numbers
        if (!cacheEntry.version.empty() && isOmniverse(parseUrl(cacheEntry.url)))
        {
            TF_DEBUG(OMNI_USD_RESOLVER)
                .Msg("%s: using version %s as timestamp for %s\n", TF_FUNC_NAME().c_str(), cacheEntry.version.c_str(),
                     cacheEntry.resolvedPath.c_str());

            // version is a string and is not guaranteed to be a number which can cause problems
            // if the version is something like "2-good" or "2-better". This will result in the timestamp
            // being 2.0 which might not properly reload.
            timestamp = TfStringToDouble(cacheEntry.version);
        }
        else
        {
            timestamp = std::chrono::duration<double>(cacheEntry.modifiedTime.time_since_epoch()).count();
        }
    }

    TF_DEBUG(OMNI_USD_RESOLVER)
        .Msg("%s: %s, %s -> %f\n", TF_FUNC_NAME().c_str(), assetPath.c_str(), cacheEntry.resolvedPath.c_str(), timestamp);

    return ArTimestamp(timestamp);
}
ArAssetInfo OmniUsdResolver::_GetAssetInfo(const std::string& assetPath, const ArResolvedPath& resolvedPath) const
{
    TF_DEBUG(OMNI_USD_RESOLVER)
        .Msg("%s: %s, %s\n", TF_FUNC_NAME().c_str(), assetPath.c_str(), resolvedPath.GetPathString().c_str());


    auto cacheEntry = _ResolveThroughCache(assetPath);

    ArAssetInfo assetInfo;
    assetInfo.version = std::move(cacheEntry.version);
    assetInfo.repoPath = cacheEntry.url; // repoPath is deprecated; use "url" within resolverInfo instead
    assetInfo.resolverInfo = VtDictionary{ { "url", VtValue(cacheEntry.url) }, { "size", VtValue(cacheEntry.size) } };

    return assetInfo;
}
std::shared_ptr<ArAsset> OmniUsdResolver::_OpenAsset(const ArResolvedPath& resolvedPath) const
{
    OMNI_TRACE_SCOPE(__FUNCTION__)
    TF_DEBUG(OMNI_USD_RESOLVER_ASSET).Msg("%s: %s\n", TF_FUNC_NAME().c_str(), resolvedPath.GetPathString().c_str());

    auto parsedUrl = parseUrl(resolvedPath.GetPathString());
    if (isLocal(parsedUrl))
    {
        TF_DEBUG(OMNI_USD_RESOLVER_ASSET)
            .Msg("%s: %s is a filesystem asset\n", TF_FUNC_NAME().c_str(), resolvedPath.GetPathString().c_str());
        return ArFilesystemAsset::Open(ArResolvedPath(fixLocalPath(parsedUrl->path)));
    }

    return OmniUsdAsset::Open(resolvedPath);
}
bool OmniUsdResolver::_CanWriteAssetToPath(const ArResolvedPath& resolvedPath, std::string* whyNot) const
{
    bool result = ResolverHelper::CanWrite(resolvedPath, whyNot);

    // we are about to write to the resolved path so remove that entry from the cache
    // once the asset has been written and re-resolved information such as the modified time and size will
    // be updated in the cache
    auto currentCache = m_threadCache.GetCurrentCache();
    if (currentCache && currentCache->Remove(resolvedPath.GetPathString()))
    {
        TF_DEBUG(OMNI_USD_RESOLVER_ASSET)
            .Msg("%s: removed %s from cache\n", TF_FUNC_NAME().c_str(), resolvedPath.GetPathString().c_str());
    }

    return result;
}
std::shared_ptr<ArWritableAsset> OmniUsdResolver::_OpenAssetForWrite(const ArResolvedPath& resolvedPath,
                                                                     WriteMode writeMode) const
{
    OMNI_TRACE_SCOPE(__FUNCTION__)
    TF_DEBUG(OMNI_USD_RESOLVER_ASSET)
        .Msg("%s: %s (writeMode=%d)\n", TF_FUNC_NAME().c_str(), resolvedPath.GetPathString().c_str(),
             static_cast<int>(writeMode));

    auto parsedUrl = parseUrl(resolvedPath.GetPathString());
    if (isLocal(parsedUrl))
    {
        TF_DEBUG(OMNI_USD_RESOLVER_ASSET)
            .Msg("%s: %s is a filesystem asset\n", TF_FUNC_NAME().c_str(), resolvedPath.GetPathString().c_str());
        return ArFilesystemWritableAsset::Create(ArResolvedPath(fixLocalPath(parsedUrl->path)), writeMode);
    }

    return OmniUsdWritableAsset::Open(resolvedPath, writeMode);
}
std::string OmniUsdResolver::_GetExtension(const std::string& assetPath) const
{
    auto parsedUri = parseUrl(assetPath);
    auto extension = TfGetExtension(parsedUri->path);

    // Check for Alembic URLs and force the "omni" extension
    if (!isLocal(parsedUri) && extension == "abc")
    {
        // OMPE-5370: Special case alembic files by forcing the "omni" extension
        // which is associated with the OmniUsdWrapperFileFormat extension.
        // The OmniUsdWrapperFileFormat will "intercept" the UsdAbcFileFormat plugin
        // and download the file before calling read / write.
        //
        // This should be removed once a proper solution is found for
        // https://github.com/PixarAnimationStudios/OpenUSD/issues/2961
        TF_DEBUG(OMNI_USD_RESOLVER).Msg("%s: %s -> omniabc\n", TF_FUNC_NAME().c_str(), assetPath.c_str());
        return "omniabc";
    }

    return extension;
}
void OmniUsdResolver::_BeginCacheScope(VtValue* cacheScopeData)
{
    m_threadCache.BeginCacheScope(cacheScopeData);
}
void OmniUsdResolver::_EndCacheScope(VtValue* cacheScopeData)
{
    m_threadCache.EndCacheScope(cacheScopeData);
}
