# sigil();

<img src="./ext/img/logo_red.png" width="200px" alt="logo">

```cpp
/* script */

use("misc/sil/array.sil")

var Writer = {
    .__init(this, ...init) {
        this.data = new Array
        this.data->add(init...)
    },

    ->__lsh(data) {
        this.data->push(data)
        return this
    },

    ->log() {
        var join = (...a) => {
            for (var i = 0, acc = ""; i < a.__len; i++) acc += a[i]
            else return acc
        }

        print(join("", this.data...))
    },
}

var w = new Writer("hello", ",")
w << " " << "world" << "!"
w->log() /* hello, world! */
```

```c
/* embed */

#include <stdio.h>
#include <stdlib.h>

#include "incl/sigil.h"

int main(void) {
    Sil *sil = sil_new(realloc);

    sil_use_slots(sil, 3);

    sil_interpret(sil, "func add(a, b) { return a + b }", 0);

    sil_variable(sil, "add", 0);
    sil_num(sil, 3, 1);
    sil_num(sil, .14, 2);
    sil_call(sil, 2, 0);

    printf("%g\n", sil_as_double(sil, 0)); /* 3.14 */

    sil_free(sil);

    return 0;
}
```
