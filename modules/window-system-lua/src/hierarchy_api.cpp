// SPDX-License-Identifier: MIT
#include "script_host_p.hpp"

namespace window_system_lua {
void ScriptHost::Private::installHierarchyApi()
{
    handlers["tabs.into_tab"] = [this](const Args& args, const Context& context) {
        return result(manager->moveTabIntoTab(target(args, 0, context, Target::Tab), identifier(args, 1),
            identifier(args, 2), placement(text(args, 3, "right"), false)));
    };
    handlers["tabs.to_group"] = [this](const Args& args, const Context& context) {
        return result(idText(manager->convertTabToGroup(target(args, 0, context, Target::Tab), placement(text(args, 1, "right"), false))));
    };
    handlers["groups.into_tab"] = [this](const Args& args, const Context& context) {
        return result(manager->moveGroupIntoTab(target(args, 0, context, Target::Group), identifier(args, 1),
            identifier(args, 2), placement(text(args, 3, "right"), false)));
    };
    handlers["groups.to_tab"] = [this](const Args& args, const Context& context) {
        return result(idText(manager->convertGroupToTab(target(args, 0, context, Target::Group), placement(text(args, 1, "right"), false))));
    };
    handlers["panes.to_tab"] = [this](const Args& args, const Context& context) {
        const auto pane = target(args, 0, context, Target::Pane);
        return result(idText(manager->extractPaneToTab(manager->model().tabForPane(pane), pane)));
    };
}
} // namespace window_system_lua
