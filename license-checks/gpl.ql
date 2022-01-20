
/**
 * @name GPL Code
 * @description Find code with a GPL license
 * @kind problem
 * @problem.severity warning
 * @precision high
 * @id license-checks/gpl-finder
 * @tags maintainability
 */

import cpp

from Comment c
where c.getContents().regexpMatch("(?si).*\\bGeneral Public License\\b.*")
select c, c.getFile()
