#pragma once

#include <memory>
#include <vector>

#include "Luma/Asset/Import/Importer.h"

namespace Luma::Assets
{
    class ImporterRegistry
    {
    public:
        void RegisterImporter(std::unique_ptr<IAssetImporter> importer);
        const IAssetImporter* ResolveBestImporter(const ImportRequest& request) const;
        std::vector<const IAssetImporter*> GetAllImporters() const;

    private:
        std::vector<std::unique_ptr<IAssetImporter>> m_Importers;
    };
}

