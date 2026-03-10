#pragma once

#include <string>
#include <string_view>
#include <utility>

namespace Luma::Editor
{
    class EditorStatusState
    {
    public:
        std::string& Content()
        {
            return m_ContentStatus;
        }

        const std::string& Content() const
        {
            return m_ContentStatus;
        }

        std::string& Sky()
        {
            return m_SkyStatus;
        }

        const std::string& Sky() const
        {
            return m_SkyStatus;
        }

        void SetProjectInput(std::string status)
        {
            m_ProjectInputStatus = std::move(status);
        }

        const std::string& GetProjectInput() const
        {
            return m_ProjectInputStatus;
        }

        void SetProjectConfig(std::string status)
        {
            m_ProjectConfigStatus = std::move(status);
        }

        std::string& ProjectConfig()
        {
            return m_ProjectConfigStatus;
        }

        const std::string& GetProjectConfig() const
        {
            return m_ProjectConfigStatus;
        }

        std::string ResolveFooterStatus(const std::string_view transientStatus = {}) const
        {
            if (!transientStatus.empty())
            {
                return std::string(transientStatus);
            }
            if (!m_ContentStatus.empty())
            {
                return m_ContentStatus;
            }
            if (!m_ProjectInputStatus.empty())
            {
                return m_ProjectInputStatus;
            }
            if (!m_ProjectConfigStatus.empty())
            {
                return m_ProjectConfigStatus;
            }
            if (!m_SkyStatus.empty())
            {
                return m_SkyStatus;
            }

            return "Ready.";
        }

        void Reset()
        {
            m_ContentStatus.clear();
            m_ProjectInputStatus.clear();
            m_ProjectConfigStatus.clear();
            m_SkyStatus.clear();
        }

    private:
        std::string m_ContentStatus;
        std::string m_ProjectInputStatus;
        std::string m_ProjectConfigStatus;
        std::string m_SkyStatus;
    };
}
