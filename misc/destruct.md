# destruct (todo)

```cpp
var destruct = {.a = "a", .b = "b", ["c"] = "c", 0, 1};

/* declaration */
var {.a, b = .b, c = ["c"], zero, one, two else 2} = destruct;

/* expression */
{.a, b = .b, c = ["c"], zero, one, two else 2} = destruct;

/* parameter */
func unbox({.a, b = .b, c = ["c"], zero, one, two else 2}) {}

/* swap */
{a, b} = {b, a};
```
