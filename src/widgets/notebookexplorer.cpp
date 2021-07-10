#include "notebookexplorer.h"

#include <QVBoxLayout>
#include <QFileDialog>
#include <QToolButton>
#include <QMenu>
#include <QActionGroup>

#include "titlebar.h"
#include "dialogs/newnotebookdialog.h"
#include "dialogs/newnotebookfromfolderdialog.h"
#include "dialogs/newfolderdialog.h"
#include "dialogs/newnotedialog.h"
#include "dialogs/managenotebooksdialog.h"
#include "dialogs/importnotebookdialog.h"
#include "dialogs/importfolderdialog.h"
#include "dialogs/importlegacynotebookdialog.h"
#include "vnotex.h"
#include "mainwindow.h"
#include "notebook/notebook.h"
#include "notebookmgr.h"
#include <utils/iconutils.h>
#include <utils/widgetutils.h>
#include <utils/pathutils.h>
#include "notebookselector.h"
#include "notebooknodeexplorer.h"
#include "messageboxhelper.h"
#include <core/configmgr.h>
#include <core/coreconfig.h>
#include <core/widgetconfig.h>
#include <core/events.h>
#include <core/exception.h>
#include <core/fileopenparameters.h>
#include "navigationmodemgr.h"
#include "widgetsfactory.h"

using namespace vnotex;

NotebookExplorer::NotebookExplorer(QWidget *p_parent)
    : QFrame(p_parent)
{
    setupUI();

    auto mainWindow = VNoteX::getInst().getMainWindow();
    connect(mainWindow, &MainWindow::mainWindowClosed,
            this, [this](const QSharedPointer<Event> &p_event) {
                if (p_event->m_handled) {
                    return;
                }

                saveSession();
            });

    connect(mainWindow, &MainWindow::mainWindowStarted,
            this, &NotebookExplorer::loadSession);
}

void NotebookExplorer::setupUI()
{
    auto mainLayout = new QVBoxLayout(this);
    WidgetUtils::setContentsMargins(mainLayout);

    // Title bar.
    auto titleBar = setupTitleBar(this);
    mainLayout->addWidget(titleBar);

    // Selector.
    m_selector = new NotebookSelector(this);
    m_selector->setWhatsThis(tr("Select one of all the notebooks as current notebook.<br/>"
                                "Move mouse on one item to check its details."));
    NavigationModeMgr::getInst().registerNavigationTarget(m_selector);
    connect(m_selector, QOverload<int>::of(&QComboBox::activated),
            this, [this](int p_idx) {
                auto id = static_cast<ID>(m_selector->itemData(p_idx).toULongLong());
                emit notebookActivated(id);
            });
    mainLayout->addWidget(m_selector);

    const auto &widgetConfig = ConfigMgr::getInst().getWidgetConfig();
    m_nodeExplorer = new NotebookNodeExplorer(this);
    m_nodeExplorer->setRecycleBinNodeVisible(widgetConfig.isNodeExplorerRecycleBinNodeVisible());
    m_nodeExplorer->setViewOrder(widgetConfig.getNodeExplorerViewOrder());
    m_nodeExplorer->setExternalFilesVisible(widgetConfig.isNodeExplorerExternalFilesVisible());
    connect(m_nodeExplorer, &NotebookNodeExplorer::nodeActivated,
            &VNoteX::getInst(), &VNoteX::openNodeRequested);
    connect(m_nodeExplorer, &NotebookNodeExplorer::fileActivated,
            &VNoteX::getInst(), &VNoteX::openFileRequested);
    connect(m_nodeExplorer, &NotebookNodeExplorer::nodeAboutToMove,
            &VNoteX::getInst(), &VNoteX::nodeAboutToMove);
    connect(m_nodeExplorer, &NotebookNodeExplorer::nodeAboutToRemove,
            &VNoteX::getInst(), &VNoteX::nodeAboutToRemove);
    connect(m_nodeExplorer, &NotebookNodeExplorer::nodeAboutToReload,
            &VNoteX::getInst(), &VNoteX::nodeAboutToReload);
    mainLayout->addWidget(m_nodeExplorer);

    setFocusProxy(m_nodeExplorer);
}

TitleBar *NotebookExplorer::setupTitleBar(QWidget *p_parent)
{
    const auto &widgetConfig = ConfigMgr::getInst().getWidgetConfig();

    auto titleBar = new TitleBar(tr("Notebook"),
                                 false,
                                 TitleBar::Action::Menu,
                                 p_parent);
    titleBar->setWhatsThis(tr("This title bar contains buttons and menu to manage notebooks and notes."));
    titleBar->setActionButtonsAlwaysShown(true);

    {
        auto viewMenu = WidgetsFactory::createMenu(titleBar);
        titleBar->addActionButton(QStringLiteral("view.svg"), tr("View"), viewMenu);
        connect(viewMenu, &QMenu::aboutToShow,
                this, [this, viewMenu]() {
                    setupViewMenu(viewMenu);
                });
    }

    {
        auto btn = titleBar->addActionButton(QStringLiteral("recycle_bin.svg"), tr("Toggle Recycle Bin Node"));
        btn->defaultAction()->setCheckable(true);
        btn->defaultAction()->setChecked(widgetConfig.isNodeExplorerRecycleBinNodeVisible());
        connect(btn, &QToolButton::triggered,
                this, [this](QAction *p_act) {
                    const bool checked = p_act->isChecked();
                    ConfigMgr::getInst().getWidgetConfig().setNodeExplorerRecycleBinNodeVisible(checked);
                    m_nodeExplorer->setRecycleBinNodeVisible(checked);
                });
    }

    titleBar->addMenuAction(QStringLiteral("manage_notebooks.svg"),
                            tr("&Manage Notebooks"),
                            titleBar,
                            [this]() {
                                ManageNotebooksDialog dialog(m_currentNotebook.data(),
                                                             VNoteX::getInst().getMainWindow());
                                dialog.exec();
                            });

    titleBar->addMenuSeparator();

    // External Files menu.
    {
        auto subMenu = titleBar->addMenuSubMenu(tr("External Files"));
        auto showAct = titleBar->addMenuAction(
            subMenu,
            tr("Show External Files"),
            titleBar,
            [this](bool p_checked) {
                ConfigMgr::getInst().getWidgetConfig().setNodeExplorerExternalFilesVisible(p_checked);
                m_nodeExplorer->setExternalFilesVisible(p_checked);
            });
        showAct->setCheckable(true);
        showAct->setChecked(widgetConfig.isNodeExplorerExternalFilesVisible());

        auto importAct = titleBar->addMenuAction(
            subMenu,
            tr("Import External Files When Activated"),
            titleBar,
            [this](bool p_checked) {
                ConfigMgr::getInst().getWidgetConfig().setNodeExplorerAutoImportExternalFilesEnabled(p_checked);
            });
        importAct->setCheckable(true);
        importAct->setChecked(widgetConfig.getNodeExplorerAutoImportExternalFilesEnabled());
    }

    return titleBar;
}

void NotebookExplorer::loadNotebooks()
{
    auto &notebookMgr = VNoteX::getInst().getNotebookMgr();
    const auto &notebooks = notebookMgr.getNotebooks();
    m_selector->setNotebooks(notebooks);

    emit updateTitleBarMenuActions();
}

void NotebookExplorer::reloadNotebook(const Notebook *p_notebook)
{
    m_selector->reloadNotebook(p_notebook);
}

void NotebookExplorer::setCurrentNotebook(const QSharedPointer<Notebook> &p_notebook)
{
    updateSession();

    m_currentNotebook = p_notebook;

    ID id = p_notebook ? p_notebook->getId() : static_cast<ID>(Notebook::InvalidId);
    m_selector->setCurrentNotebook(id);

    m_nodeExplorer->setNotebook(p_notebook);

    recoverSession();

    emit updateTitleBarMenuActions();
}

void NotebookExplorer::newNotebook()
{
    NewNotebookDialog dialog(VNoteX::getInst().getMainWindow());
    dialog.exec();
}

void NotebookExplorer::importNotebook()
{
    ImportNotebookDialog dialog(VNoteX::getInst().getMainWindow());
    dialog.exec();
}

void NotebookExplorer::newFolder()
{
    auto node = checkNotebookAndGetCurrentExploredFolderNode();
    if (!node) {
        return;
    }

    if (m_currentNotebook->isRecycleBinNode(node)) {
        node = m_currentNotebook->getRootNode().data();
    } else if (m_currentNotebook->isNodeInRecycleBin(node)) {
        MessageBoxHelper::notify(MessageBoxHelper::Information,
                                 tr("Could not create folder within Recycle Bin."),
                                 VNoteX::getInst().getMainWindow());
        return;
    }

    NewFolderDialog dialog(node, VNoteX::getInst().getMainWindow());
    if (dialog.exec() == QDialog::Accepted) {
        m_nodeExplorer->setCurrentNode(dialog.getNewNode().data());
    }
}

void NotebookExplorer::newNote()
{
    auto node = checkNotebookAndGetCurrentExploredFolderNode();
    if (!node) {
        return;
    }

    if (m_currentNotebook->isRecycleBinNode(node)) {
        node = m_currentNotebook->getRootNode().data();
    } else if (m_currentNotebook->isNodeInRecycleBin(node)) {
        MessageBoxHelper::notify(MessageBoxHelper::Information,
                                 tr("Could not create note within Recycle Bin."),
                                 VNoteX::getInst().getMainWindow());
        return;
    }

    NewNoteDialog dialog(node, VNoteX::getInst().getMainWindow());
    if (dialog.exec() == QDialog::Accepted) {
        m_nodeExplorer->setCurrentNode(dialog.getNewNode().data());

        // Open it right now.
        auto paras = QSharedPointer<FileOpenParameters>::create();
        paras->m_mode = ViewWindowMode::Edit;
        paras->m_newFile = true;
        emit VNoteX::getInst().openNodeRequested(dialog.getNewNode().data(), paras);
    }
}

Node *NotebookExplorer::currentExploredFolderNode() const
{
    return m_nodeExplorer->currentExploredFolderNode();
}

Node *NotebookExplorer::currentExploredNode() const
{
    return m_nodeExplorer->currentExploredNode();
}

Node *NotebookExplorer::checkNotebookAndGetCurrentExploredFolderNode() const
{
    if (!m_currentNotebook) {
        MessageBoxHelper::notify(MessageBoxHelper::Information,
                                 tr("Please first create a notebook to hold your data."),
                                 VNoteX::getInst().getMainWindow());
        return nullptr;
    }

    auto node = currentExploredFolderNode();
    Q_ASSERT(m_currentNotebook.data() == node->getNotebook());
    return node;
}

void NotebookExplorer::newNotebookFromFolder()
{
    NewNotebookFromFolderDialog dialog(VNoteX::getInst().getMainWindow());
    dialog.exec();
}

void NotebookExplorer::importFile()
{
    auto node = checkNotebookAndGetCurrentExploredFolderNode();
    if (!node) {
        return;
    }

    if (m_currentNotebook->isRecycleBinNode(node)) {
        node = m_currentNotebook->getRootNode().data();
    } else if (m_currentNotebook->isNodeInRecycleBin(node)) {
        MessageBoxHelper::notify(MessageBoxHelper::Information,
                                 tr("Could not create note within Recycle Bin."),
                                 VNoteX::getInst().getMainWindow());
        return;
    }

    static QString lastFolderPath = QDir::homePath();
    QStringList files = QFileDialog::getOpenFileNames(VNoteX::getInst().getMainWindow(),
                                                      tr("Select Files To Import"),
                                                      lastFolderPath);
    if (files.isEmpty()) {
        return;
    }

    QString errMsg;
    for (const auto &file : files) {
        try {
            m_currentNotebook->copyAsNode(node, Node::Flag::Content, file);
        } catch (Exception &p_e) {
            errMsg += tr("Failed to add file (%1) as node (%2).\n").arg(file, p_e.what());
        }
    }

    if (!errMsg.isEmpty()) {
        MessageBoxHelper::notify(MessageBoxHelper::Critical, errMsg, VNoteX::getInst().getMainWindow());
    }

    emit m_currentNotebook->nodeUpdated(node);
    m_nodeExplorer->setCurrentNode(node);
}

void NotebookExplorer::importFolder()
{
    auto node = checkNotebookAndGetCurrentExploredFolderNode();
    if (!node) {
        return;
    }

    if (m_currentNotebook->isRecycleBinNode(node)) {
        node = m_currentNotebook->getRootNode().data();
    } else if (m_currentNotebook->isNodeInRecycleBin(node)) {
        MessageBoxHelper::notify(MessageBoxHelper::Information,
                                 tr("Could not create folder within Recycle Bin."),
                                 VNoteX::getInst().getMainWindow());
        return;
    }

    ImportFolderDialog dialog(node, VNoteX::getInst().getMainWindow());
    if (dialog.exec() == QDialog::Accepted) {
        m_nodeExplorer->setCurrentNode(dialog.getNewNode().data());
    }
}

void NotebookExplorer::importLegacyNotebook()
{
    ImportLegacyNotebookDialog dialog(VNoteX::getInst().getMainWindow());
    dialog.exec();
}

void NotebookExplorer::locateNode(Node *p_node)
{
    Q_ASSERT(p_node);
    auto nb = p_node->getNotebook();
    if (nb != m_currentNotebook) {
        emit notebookActivated(nb->getId());
    }
    m_nodeExplorer->setCurrentNode(p_node);
    m_nodeExplorer->setFocus();
}

const QSharedPointer<Notebook> &NotebookExplorer::currentNotebook() const
{
    return m_currentNotebook;
}

void NotebookExplorer::setupViewMenu(QMenu *p_menu)
{
    if (!p_menu->isEmpty()) {
        return;
    }

    auto ag = new QActionGroup(p_menu);

    auto act = ag->addAction(tr("View By Configuration"));
    act->setCheckable(true);
    act->setChecked(true);
    act->setData(NotebookNodeExplorer::ViewOrder::OrderedByConfiguration);
    p_menu->addAction(act);

    act = ag->addAction(tr("View By Name"));
    act->setCheckable(true);
    act->setData(NotebookNodeExplorer::ViewOrder::OrderedByName);
    p_menu->addAction(act);

    act = ag->addAction(tr("View By Name (Reversed)"));
    act->setCheckable(true);
    act->setData(NotebookNodeExplorer::ViewOrder::OrderedByNameReversed);
    p_menu->addAction(act);

    act = ag->addAction(tr("View By Created Time"));
    act->setCheckable(true);
    act->setData(NotebookNodeExplorer::ViewOrder::OrderedByCreatedTime);
    p_menu->addAction(act);

    act = ag->addAction(tr("View By Created Time (Reversed)"));
    act->setCheckable(true);
    act->setData(NotebookNodeExplorer::ViewOrder::OrderedByCreatedTimeReversed);
    p_menu->addAction(act);

    act = ag->addAction(tr("View By Modified Time"));
    act->setCheckable(true);
    act->setData(NotebookNodeExplorer::ViewOrder::OrderedByModifiedTime);
    p_menu->addAction(act);

    act = ag->addAction(tr("View By Modified Time (Reversed)"));
    act->setCheckable(true);
    act->setData(NotebookNodeExplorer::ViewOrder::OrderedByModifiedTimeReversed);
    p_menu->addAction(act);

    int viewOrder = ConfigMgr::getInst().getWidgetConfig().getNodeExplorerViewOrder();
    for (const auto &act : ag->actions()) {
        if (act->data().toInt() == viewOrder) {
            act->setChecked(true);
        }
    }

    connect(ag, &QActionGroup::triggered,
            this, [this](QAction *p_action) {
                int order = p_action->data().toInt();
                ConfigMgr::getInst().getWidgetConfig().setNodeExplorerViewOrder(order);
                m_nodeExplorer->setViewOrder(order);
            });
}

void NotebookExplorer::saveSession()
{
    updateSession();

    auto &sessionConfig = ConfigMgr::getInst().getSessionConfig();
    sessionConfig.setNotebookExplorerSession(m_session.serialize());
}

void NotebookExplorer::loadSession()
{
    auto &sessionConfig = ConfigMgr::getInst().getSessionConfig();
    m_session = NotebookExplorerSession::deserialize(sessionConfig.getNotebookExplorerSessionAndClear());

    m_sessionLoaded = true;

    recoverSession();
}

void NotebookExplorer::updateSession()
{
    if (!m_sessionLoaded || !m_currentNotebook) {
        return;
    }

    auto& nbSession = m_session.m_notebooks[m_currentNotebook->getRootFolderPath()];
    nbSession.m_recovered = true;

    auto node = currentExploredNode();
    if (node) {
        nbSession.m_currentNodePath = node->fetchPath();
    } else {
        nbSession.m_currentNodePath.clear();
    }
}

void NotebookExplorer::recoverSession()
{
    if (!m_sessionLoaded || !m_currentNotebook) {
        return;
    }

    auto it = m_session.m_notebooks.find(m_currentNotebook->getRootFolderPath());
    if (it != m_session.m_notebooks.end()) {
        if (it.value().m_recovered || it.value().m_currentNodePath.isEmpty()) {
            return;
        }

        it.value().m_recovered = true;

        auto node = m_currentNotebook->loadNodeByPath(it.value().m_currentNodePath);
        if (node) {
            m_nodeExplorer->setCurrentNode(node.data());
        }
    }
}
