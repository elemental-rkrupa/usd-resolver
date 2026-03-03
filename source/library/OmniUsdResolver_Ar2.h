// SPDX-FileCopyrightText: Copyright (c) 2018-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: LicenseRef-NvidiaProprietary
//
// NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
// property and proprietary rights in and to this material, related
// documentation and any modifications thereto. Any use, reproduction,
// disclosure or distribution of this material and related documentation
// without an express license agreement from NVIDIA CORPORATION or
// its affiliates is strictly prohibited.

#pragma once

#include "OmniUsdResolverCache.h"
#include "../../include/Defines.h"
#include <pxr/usd/ar/asset.h>
#include <pxr/usd/ar/resolvedPath.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/ar/resolverContext.h>
#include <pxr/usd/ar/writableAsset.h>

/// \brief The Ar 2 implementation of the Omniverse Usd Resolver
class OMNIUSDRESOLVER_EXPORT_CPP OmniUsdResolver final : public PXR_NS::ArResolver
{
public:
    OmniUsdResolver();
    virtual ~OmniUsdResolver();

protected:
    // --------------------------------------------------------------------- //
    /// \anchor ArResolver_identifiers
    /// \name Identifier Operations
    ///
    /// \brief Identifiers are an important part of resolution that correlate
    /// the identity of a layer to it's backing asset management system.
    /// An identifier could be an absolute file path, a URL, a UUID
    /// or some other unique syntax.
    /// @{
    // --------------------------------------------------------------------- //

    /// \brief Creates an identifier for the \p assetPath using anchorAssetPath.
    ///
    /// if \p assetPath is relative, \p anchorAssetPath will be used for anchoring
    ///
    /// If \p anchorAssetPath ends with a trailing '/', it is treated as
    /// a directory to which \p assetPath will be anchored. Otherwise, it
    /// is treated as a file and \p assetPath will be anchored to its
    /// containing directory.
    ///
    /// if \p assetPath refers to a fully-qualified URL or an absolute file path, \p anchorAssetPath
    /// will not be used.
    ///
    /// if \p assetPath is a builtin path (i.e nvidia/aux-definitions.mdl), set by
    /// \ref omniUsdResolverSetMdlBuiltins, then \p assetPath will be returned as-is.
    ///
    /// The returned identifier will be in its final normalized form
    std::string _CreateIdentifier(const std::string& assetPath,
                                  const PXR_NS::ArResolvedPath& anchorAssetPath) const override;
    /// \brief Creates an identifier for \p assetPath that may only exist in memory
    ///
    /// This is functionally equivalent to _CreateIdentifier in our implementation.
    /// We do not do any existance-checks when creating an identifier
    ///
    /// The returned identifier will be in its final normalized form
    std::string _CreateIdentifierForNewAsset(const std::string& assetPath,
                                             const PXR_NS::ArResolvedPath& anchorAssetPath) const override;
    /// @}

    // --------------------------------------------------------------------- //
    /// \anchor ArResolver_resolution
    /// \name Resolving Operations
    ///
    /// @{
    // --------------------------------------------------------------------- //

    /// \brief Resolves \p assetPath to it's final location where the asset is stored.
    ///
    /// If the \assetPath can not be resolved, an empty ArResolvedPath will be returned
    ///
    /// This is not always guaranteed to be a file path on disk.
    ///
    /// \param assetPath the asset to resolve, typically the identifier
    PXR_NS::ArResolvedPath _Resolve(const std::string& assetPath) const override;

    /// \brief Resolves \p assetPath to it's final location where the asset can be stored.
    ///
    /// If the \assetPath can not be resolved, an empty ArResolvedPath will be returned
    ///
    /// This is not always guaranteed to be a file path on disk.
    ///
    /// \param assetPath the asset to resolve, typically the identifier
    PXR_NS::ArResolvedPath _ResolveForNewAsset(const std::string& assetPath) const override;

    /// @}

    // --------------------------------------------------------------------- //
    /// \anchor ArResolver_context
    /// \name Context Operations
    ///
    /// \brief Contexts are a place to store implementation specific information that can be
    /// configured through the public Ar API. Information such as a global set of search paths
    /// to resolve against, sequence / shot data, the root asset of the current stage, etc.
    ///
    /// The use of Contexts in \c OmniUsdResolver is limited and is only implemented to store
    /// the root asset
    ///
    /// @{
    // --------------------------------------------------------------------- //
    PXR_NS::ArResolverContext _CreateDefaultContext() const override;
    PXR_NS::ArResolverContext _CreateDefaultContextForAsset(const std::string& assetPath) const override;
    PXR_NS::ArResolverContext _CreateContextFromString(const std::string& contextStr) const override;

    bool _IsContextDependentPath(const std::string& assetPath) const override;
    void _RefreshContext(const PXR_NS::ArResolverContext& context) override;

    void _BindContext(const PXR_NS::ArResolverContext& context, PXR_NS::VtValue* bindingData) override;
    void _UnbindContext(const PXR_NS::ArResolverContext& context, PXR_NS::VtValue* bindingData) override;
    PXR_NS::ArResolverContext _GetCurrentContext() const override;

    /// @}

    // --------------------------------------------------------------------- //
    /// \anchor ArResolver_cache
    /// \name Caching Operations
    ///
    /// @{
    // --------------------------------------------------------------------- //
    void _BeginCacheScope(PXR_NS::VtValue* cacheScopeData) override;
    void _EndCacheScope(PXR_NS::VtValue* cacheScopeData) override;

    /// @}

    // --------------------------------------------------------------------- //
    /// \anchor ArResolver_asset
    /// \name Asset Operations
    ///
    /// @{
    // --------------------------------------------------------------------- //
    /// \brief Performs any necessary path parsing and returns the extension of \p assetPath
    /// \param assetPath the path to get the extension for
    /// \return the extension of the path without the '.'
    std::string _GetExtension(const std::string& assetPath) const override;
    /// \brief Returns the timestamp when \p assetPath was last modified.
    /// \note Precision of the timestamp can very depending on the backing asset management system. Nucleus
    /// only supports to the nearest second whereas a file on disk may support nanoseconds
    PXR_NS::ArTimestamp _GetModificationTimestamp(const std::string& assetPath,
                                                  const PXR_NS::ArResolvedPath& resolvedPath) const override;
    /// Returns the asset information associated with \p assetPath when it was resolved
    PXR_NS::ArAssetInfo _GetAssetInfo(const std::string& assetPath,
                                      const PXR_NS::ArResolvedPath& resolvedPath) const override;

    /// Opens the resolved asset for reading
    std::shared_ptr<PXR_NS::ArAsset> _OpenAsset(const PXR_NS::ArResolvedPath& resolvedPath) const override;
    /// Determines if the resolved asset can be written to
    bool _CanWriteAssetToPath(const ArResolvedPath& resolvedPath, std::string* whyNot) const override;
    /// Opens the resolved asset for writing
    std::shared_ptr<PXR_NS::ArWritableAsset> _OpenAssetForWrite(const PXR_NS::ArResolvedPath& resolvedPath,
                                                                WriteMode writeMode) const override;
    /// @}

private:
    mutable OmniUsdResolverScopedCache m_threadCache;

    OmniUsdResolverCache::Entry _ResolveThroughCache(const std::string& identifier) const;
};
