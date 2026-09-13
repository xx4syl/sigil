# operator 'new'

```cpp
var Unit = {
    .__init = (this, name) => {
        this.name = name ? name : "unnamed";
    },
};

var u = new Unit("alpha");

/* equals to */

var u = (Unit){};
Unit.__init(u, "alpha");
```
