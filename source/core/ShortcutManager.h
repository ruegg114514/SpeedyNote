#ifndef SHORTCUTMANAGER_H
#define SHORTCUTMANAGER_H

#include <QObject>
#include <QString>
#include <QHash>
#include <QKeySequence>
#include <QStringList>
#include <QAction>

/**
 * @brief Centralized keyboard shortcut management system.
 * 
 * ShortcutManager is a singleton that manages all keyboard shortcuts in SpeedyNote.
 * It provides:
 * - Registration of actions with default shortcuts
 * - User customization (overrides stored in shortcuts.json)
 * - Conflict detection
 * - Signal emission when shortcuts change
 * 
 * Usage:
 *   // Register an action (typically in initialization)
 *   ShortcutManager::instance()->registerAction("file.save", "Ctrl+S", 
 *                                                tr("Save Document"), "File");
 *   
 *   // Get the current shortcut (respects user overrides)
 *   QKeySequence seq = ShortcutManager::instance()->keySequenceForAction("file.save");
 *   
 *   // Listen for changes
 *   connect(ShortcutManager::instance(), &ShortcutManager::shortcutChanged,
 *           this, &MyClass::onShortcutChanged);
 */
class ShortcutManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Get the singleton instance.
     * Creates the instance on first call.
     */
    static ShortcutManager* instance();
    
    /**
     * @brief Register all default shortcuts.
     * Called once during application initialization.
     * Must be called AFTER instance() but BEFORE any shortcut lookups.
     */
    void registerDefaults();
    
    /**
     * @brief Scope for shortcut conflict detection (public for registerAction).
     */
    enum class Scope {
        Global,       ///< Applies to both paged and edgeless documents
        PagedOnly,    ///< Only applies to paged documents
        EdgelessOnly  ///< Only applies to edgeless documents
    };
    
    /**
     * @brief Register an action with its default shortcut.
     * 
     * @param actionId Unique identifier (e.g., "file.save", "tool.pen")
     * @param defaultShortcut Default key sequence string (e.g., "Ctrl+S", "B")
     * @param displayName Human-readable name for UI (e.g., "Save Document")
     * @param category Category for grouping in UI (e.g., "File", "Tools")
     * @param scope Document mode scope for conflict detection (default: Global)
     * 
     * If the action is already registered, this updates the default/display/category
     * but preserves any user override.
     */
    void registerAction(const QString& actionId,
                        const QString& defaultShortcut,
                        const QString& displayName,
                        const QString& category,
                        Scope scope = Scope::Global);
    
    /**
     * @brief Check if an action is registered.
     */
    bool hasAction(const QString& actionId) const;
    
    // ========== Shortcut Retrieval ==========
    
    /**
     * @brief Get the current shortcut string for an action.
     * Returns user override if set, otherwise the default.
     * Returns empty string if action not registered.
     */
    QString shortcutForAction(const QString& actionId) const;
    
    /**
     * @brief Get the current shortcut as a QKeySequence.
     * Convenience method for use with QShortcut.
     */
    QKeySequence keySequenceForAction(const QString& actionId) const;
    
    /**
     * @brief Get the default shortcut for an action.
     */
    QString defaultShortcutForAction(const QString& actionId) const;
    
    /**
     * @brief Check if the action has a user override.
     */
    bool isUserOverridden(const QString& actionId) const;
    
    // ========== User Customization ==========
    
    /**
     * @brief Set a user override for an action's shortcut.
     * 
     * @param actionId The action to modify
     * @param shortcut New shortcut string (e.g., "Ctrl+Shift+S")
     * 
     * Emits shortcutChanged signal. Does NOT auto-save; call saveUserShortcuts().
     */
    void setUserShortcut(const QString& actionId, const QString& shortcut);
    
    /**
     * @brief Clear user override, reverting to default.
     * Emits shortcutChanged signal if the shortcut actually changes.
     */
    void clearUserShortcut(const QString& actionId);
    
    /**
     * @brief Reset all shortcuts to their defaults.
     * Clears all user overrides. Emits shortcutChanged for each changed shortcut.
     */
    void resetAllToDefaults();
    
    // ========== Persistence ==========
    
    /**
     * @brief Load user shortcuts from shortcuts.json.
     * Called automatically on first instance() access.
     */
    void loadUserShortcuts();
    
    /**
     * @brief Save user shortcuts to shortcuts.json.
     * Only saves overrides, not defaults.
     */
    void saveUserShortcuts();
    
    /**
     * @brief Get the path to the shortcuts config file.
     */
    QString configFilePath() const;
    
    // ========== Conflict Detection ==========
    
    /**
     * @brief Find actions that use the given shortcut.
     * 
     * @param shortcut Shortcut string to check
     * @param excludeActionId Optional action to exclude from check
     * @return List of action IDs that use this shortcut
     */
    QStringList findConflicts(const QString& shortcut, 
                              const QString& excludeActionId = QString()) const;
    
    // ========== UI Helpers ==========
    
    /**
     * @brief Get all registered action IDs.
     */
    QStringList allActionIds() const;
    
    /**
     * @brief Get all unique categories.
     */
    QStringList allCategories() const;
    
    /**
     * @brief Get action IDs in a specific category.
     */
    QStringList actionsInCategory(const QString& category) const;
    
    /**
     * @brief Get the display name for an action.
     */
    QString displayNameForAction(const QString& actionId) const;
    
    /**
     * @brief Get the category for an action.
     */
    QString categoryForAction(const QString& actionId) const;

    // ========== QAction Registry (MAC.1) ==========

    /**
     * @brief Get (or lazily create) a QAction for the given action ID.
     *
     * The QAction is parented to ShortcutManager and lives for the application
     * lifetime. Its shortcut, context, display name, and enabled state are
     * managed by ShortcutManager. The QAction is not added to any widget by
     * default — its shortcut will not fire until a consumer (menu bar,
     * MainWindow, toolbar) calls widget->addAction(action) or adds it to a
     * QMenu in a visible menu bar.
     *
     * Returns nullptr if actionId is not registered.
     *
     * Per QA Q6.5.1: callers connect to QAction::triggered using the standard
     * Qt connect() idiom; ShortcutManager does not provide handler-registration
     * sugar.
     */
    QAction* action(const QString& actionId);

    /**
     * @brief Set a macOS-only default shortcut for an action.
     *
     * On macOS, the macOS default takes precedence over the cross-platform
     * default (but is still overridden by a user override). On other
     * platforms, this value is stored but has no effect.
     *
     * Per QA Q3.2 / Q4.3.d / Q4.5: used for app.settings (Ctrl+,),
     * view.fullscreen (Ctrl+Meta+F = Cmd+Ctrl+F), and
     * layer.toggle_visibility (Ctrl+;).
     */
    void setMacosDefault(const QString& actionId, const QString& shortcut);

    /**
     * @brief Override the QAction shortcut context for a specific action.
     *
     * Default context for new QActions is Qt::WindowShortcut (per QA Q6.5.2).
     * Letter-key tool shortcuts (B/E/L/T/M/V/I) may need
     * Qt::WidgetWithChildrenShortcut to avoid firing while typing in text
     * widgets — to be applied during MAC.7. Unused in MAC.1 itself.
     */
    void setActionContext(const QString& actionId, Qt::ShortcutContext ctx);

    /**
     * @brief Set the currently-active document scope.
     *
     * Enables/disables every registered QAction based on its declared scope:
     *   - Scope::Global actions are always enabled
     *   - Scope::PagedOnly actions are enabled iff scope == PagedOnly
     *   - Scope::EdgelessOnly actions are enabled iff scope == EdgelessOnly
     *   - When scope == Global, all actions are enabled (e.g., no document open)
     *
     * Per QA Q6.5.5: ShortcutManager owns scope enforcement. MainWindow plumbs
     * SplitViewManager::activeViewportChanged into this setter.
     */
    void setActiveDocumentScope(Scope scope);

    /**
     * @brief Get the currently-active document scope.
     */
    Scope activeDocumentScope() const { return m_activeScope; }

signals:
    /**
     * @brief Emitted when a shortcut changes (user override or clear).
     * 
     * @param actionId The action whose shortcut changed
     * @param newShortcut The new shortcut string (may be empty if cleared)
     */
    void shortcutChanged(const QString& actionId, const QString& newShortcut);

private:
    // Private constructor for singleton
    explicit ShortcutManager(QObject* parent = nullptr);
    ~ShortcutManager() override = default;
    
    // Prevent copying
    ShortcutManager(const ShortcutManager&) = delete;
    ShortcutManager& operator=(const ShortcutManager&) = delete;
    
    /**
     * @brief Internal structure for shortcut data.
     */
    struct ShortcutEntry {
        QString defaultShortcut;   ///< The built-in default
        QString macosDefault;      ///< MAC.1: optional macOS-only default (takes precedence over defaultShortcut on macOS)
        QString userShortcut;      ///< User override (empty = use default)
        QString displayName;       ///< Human-readable name for UI
        QString category;          ///< Category for grouping
        Scope scope = Scope::Global;  ///< Document mode scope for conflict detection
        Qt::ShortcutContext context = Qt::WindowShortcut;  ///< MAC.1: shortcut context for the QAction (Q6.5.2)
        QAction* action = nullptr; ///< MAC.1: lazily-created QAction parented to ShortcutManager
    };
    
    /**
     * @brief Check if two scopes can conflict.
     * 
     * Global conflicts with everything. PagedOnly and EdgelessOnly don't
     * conflict with each other because they're mutually exclusive.
     */
    static bool scopesCanConflict(Scope a, Scope b);

    /**
     * @brief MAC.1: Whether an entry should be enabled for the given active scope.
     *
     * Returns true if:
     *   - the entry is Global (always available), or
     *   - the active scope is Global (e.g. no document open — enable everything), or
     *   - the entry's scope matches the active scope (PagedOnly+PagedOnly or
     *     EdgelessOnly+EdgelessOnly).
     */
    static bool isEnabledForActiveScope(Scope entryScope, Scope activeScope);
    
    /// All registered shortcuts, keyed by action ID
    QHash<QString, ShortcutEntry> m_shortcuts;

    /// MAC.1: currently-active document scope (drives QAction enable/disable)
    Scope m_activeScope = Scope::Global;

    /// Path to shortcuts.json
    QString m_configPath;
    
    /// Singleton instance
    static ShortcutManager* s_instance;
};

#endif // SHORTCUTMANAGER_H

