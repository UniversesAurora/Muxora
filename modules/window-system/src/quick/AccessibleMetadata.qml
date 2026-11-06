// SPDX-License-Identifier: MIT
import QtQuick

// Attach Qt's public accessibility metadata to existing C++ scene items.
QtObject {
    function attach(target: Item): QtObject { return target.Accessible }
}
