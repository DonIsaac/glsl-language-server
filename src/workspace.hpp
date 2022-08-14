#ifndef WORKSPACE_H
#define WORKSPACE_H

#include <map>
#include <string>
#include <utility>

class Workspace
{

public:
    Workspace();
    virtual ~Workspace();

    bool is_initialized();
    void set_initialized(bool new_value);

    std::map<std::string, std::string> &documents();
    void add_document(std::string key, std::string text);
    bool remove_document(std::string key);
    /**
     * @brief Updates the contents of a document within the workspace.
     *
     * @param key The name of the document
     * @param text The new contents of the document
     *
     * @return true if a document exists under `key` is updated.
     * @return false otherwise
     */
    bool change_document(std::string key, std::string text);

private:
    bool m_initialized = false;
    std::map<std::string, std::string> m_documents;
};

#endif /* WORKSPACE_H */
