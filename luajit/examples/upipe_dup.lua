local upipe = require "upipe"

return upipe {
    sub_mgr = upipe {
        sub = true,
        output = true,

        init = function (pipe)
            local super = pipe:sub_get_super()
            if super.helper.flow_def ~= nil then
                pipe:helper_store_flow_def(super.helper.flow_def:dup())
            end
        end
    },

    input = function (pipe, ref, upump_p)
        for output in pipe:iterate_sub() do
            output:helper_output(ref:dup(), upump_p)
        end
        ref:free()
    end,

    control = {
        set_flow_def = function (pipe, flow_def)
            pipe:helper_store_flow_def(flow_def:dup())
            for output in pipe:iterate_sub() do
                output:helper_store_flow_def(flow_def:dup())
            end
        end
    }
}
