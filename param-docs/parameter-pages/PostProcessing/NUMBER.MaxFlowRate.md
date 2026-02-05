# Parameter `MaxFlowValue`
Default Value: `4,0`

Only the relevant ROI's are evaluated (which change with `PreValue` +/- `MaxFlowValue`), the remaining ROI's retain the old value.

!!! Warning
    Experimental function, description will come later.

!!! Warning
    This is an **Expert Parameter**! Only change it if you understand what it does!

!!! Note
    If you edit the config file manually, you must prefix this parameter with `<NUMBER>` followed by a dot (eg. `main.MaxFlowValue`). The reason is that this parameter is specific for each `<NUMBER>` (`<NUMBER>` is the name of the number sequence defined in the ROI's).
