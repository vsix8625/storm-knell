vim.opt_local.expandtab = true
vim.opt_local.tabstop = 4
vim.opt_local.shiftwidth = 4
vim.opt_local.softtabstop = 4
vim.opt_local.autoindent = true

local group = vim.api.nvim_create_augroup("StormFormatOnSave", { clear = true })

vim.api.nvim_create_autocmd("BufWritePre", {
	group = group,
	buffer = 0,
	callback = function()
		local lines = vim.api.nvim_buf_get_lines(0, 0, -1, false)
		local result = {}
		local depth = 0
		local prop_indent = 0
		local indent = "    "

		for _, line in ipairs(lines) do
			local stripped = line:match("^%s*(.-)%s*$")

			if stripped == "" then
				prop_indent = 0
				table.insert(result, "")
			else
				if stripped:match("^}") then
					depth = math.max(0, depth - 1)
					prop_indent = 0
				elseif stripped:match("^%w+::?") or stripped:match("^if%s*%(") then
					prop_indent = 0
				end

				local total_depth = depth + prop_indent
				table.insert(result, string.rep(indent, total_depth) .. stripped)

				if stripped:match("{%s*$") then
					depth = depth + 1
				elseif stripped:match("::?%s*$") then
					prop_indent = 1
				end
			end
		end

		vim.api.nvim_buf_set_lines(0, 0, -1, false, result)
	end,
})
