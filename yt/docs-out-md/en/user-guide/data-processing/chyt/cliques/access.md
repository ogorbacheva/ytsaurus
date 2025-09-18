---
metadata:
  - name: generator
    content: Diplodoc Platform v5.5.3
csp:
  - script-src-elem:
      - https://mc.yandex.ru
  - connect-src:
      - https://*.algolia.net
      - https://*.algolianet.com
vcsPath: en/user-guide/data-processing/chyt/cliques/access.md
sourcePath: en/user-guide/data-processing/chyt/cliques/access.md
---
# Access permissions

This article describes the clique access rules and how to request them.

## Authentication { #authentication }

Users are authenticated in CHYT in the same way as in YTsaurus — using a token from YTsaurus (for more information, see [Authentication](../../../../user-guide/storage/auth.md)).

## Access types { #access_types }

Upon receiving a query, CHYT checks for two types of permissions:

1. **Permissions to access clique resources** </br>
   When receiving a query, the clique checks whether the user that made the query has the permission to use the clique.
   - To execute SQL queries, you need the `use` permission.
   - To modify, view, and delete a clique configuration, you need the `manage`, `read`, and `remove` permissions.

   Clique permissions are stored in a dedicated system node called Access Control Object. It's located at the path `//sys/access_control_object_namespaces/chyt/<alias>/principal` and is generated automatically for each clique created with the [CHYT Controller](../cliques/controller.md).

   Clique permissions only control access to the computational resources of the cliques themselves.

2. **Data access permissions** </br>
   When accessing any data in YTsaurus, the user's read/write access to all referenced tables is verified according to the standard ACL mechanism implemented in YTsaurus. This verification ensures secure data access.
