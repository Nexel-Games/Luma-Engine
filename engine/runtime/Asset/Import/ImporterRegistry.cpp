#include "Luma/Asset/Import/ImporterRegistry.h"

#include <algorithm>

namespace Luma::Assets
{
    void ImporterRegistry::RegisterImporter(std::unique_ptr<IAssetImporter> importer)
    {
        if (!importer)
        {
            return;
        }

        m_Importers.push_back(std::move(importer));
        std::sort(
            m_Importers.begin(),
            m_Importers.end(),
            [](const auto& lhs, const auto& rhs)
            {
                return lhs->GetPriority() > rhs->GetPriority();
            });
    }

    const IAssetImporter* ImporterRegistry::ResolveBestImporter(const ImportRequest& request) const
    {
        for (const auto& importer : m_Importers)
        {
            if (importer->CanHandle(request))
            {
                return importer.get();
            }
        }

        return nullptr;
    }

    std::vector<const IAssetImporter*> ImporterRegistry::GetAllImporters() const
    {
        std::vector<const IAssetImporter*> importers;
        importers.reserve(m_Importers.size());
        for (const auto& importer : m_Importers)
        {
            importers.push_back(importer.get());
        }
        return importers;
    }
}

