"""Permissions commands"""

from .common import set_param
from .driver import make_request, set_read_from_params
from .transaction_commands import _make_formatted_transactional_request

def check_permission(user, permission, path, format=None, read_from=None, cache_sticky_group_size=None, columns=None, client=None):
    """Checks permission for Cypress node.

    :param str user: user login.
    :param str permission: one of ["read", "write", "administer", "create", "use"].
    :return: permission in specified format (YSON by default).

    .. seealso:: `permissions in the docs <https://yt.yandex-team.ru/docs/description/common/access_control>`_
    """
    params = {
        "user": user,
        "permission": permission,
        "path": path
    }
    set_read_from_params(params, read_from, cache_sticky_group_size)
    set_param(params, "columns", columns)
    return _make_formatted_transactional_request(
        "check_permission",
        params,
        format=format,
        client=client)

def add_member(member, group, client=None):
    """Adds member to Cypress node group.

    :param str member: member to add.
    :param str group: group to add member to.

    .. seealso:: `permissions in the docs <https://yt.yandex-team.ru/docs/description/common/access_control>`_
    """
    return make_request(
        "add_member",
        {
            "member": member,
            "group": group
        },
        client=client)

def remove_member(member, group, client=None):
    """Removes member from Cypress node group.

    :param str member: member to remove.
    :param str group: group to remove member from.

    .. seealso:: `permissions in the docs <https://yt.yandex-team.ru/docs/description/common/access_control>`_
    """
    return make_request(
        "remove_member",
        {
            "member": member,
            "group": group
        },
        client=client)
