if vim.b.current_syntax then return end
vim.cmd("syntax clear")
vim.opt_local.spell = false

local keywords = {
	"cc",
	"linker",
	"sources",
	"includes",
	"if",
	"else",
	"target",
	"codegen",
	"cflags",
	"lflags",
	"defines",
	"define",
	"literal",
	"mode",
	"kind",
	"depends",
	"exclude",
	"out",
	"out_dir",
	"print",
	"install",
	"exit",
}

vim.cmd([[syntax match stormComment "//.*$"]])

vim.cmd("syntax keyword stormKeyword " .. table.concat(keywords, " "))

vim.cmd([[syntax match stormKey "\<\w\+\>\ze::\?"]])

vim.cmd([[syntax match stormOperator "::\?"]])

vim.cmd([[syntax match stormFlag "-\w\+[-=\w]*"]])
vim.cmd([[syntax match stormPath "\<\w\+[\/\w\.]\+\>"]])

vim.cmd([[syntax region stormString start=/"/ skip=/\\"/ end=/"/]])
vim.cmd([[syntax region stormString start=/'/ skip=/\\'/ end=/'/]])

vim.cmd([[syntax match stormNumber "\<\d\+\>"]])

vim.cmd([[syntax match stormBrace "[{}]"]])
vim.cmd([[syntax match stormBuiltin "\<__\w\+__\>"]])
vim.cmd([[syntax keyword stormBoolean true false]])

vim.cmd([[highlight default link stormComment     Comment]])
vim.cmd([[highlight default link stormKeyword     Keyword]])
vim.cmd([[highlight default link stormKey         Identifier]])
vim.cmd([[highlight default link stormOperator    Operator]])
vim.cmd([[highlight default link stormString      String]])
vim.cmd([[highlight default link stormNumber      Number]])
vim.cmd([[highlight default link stormFlag        PreProc]])
vim.cmd([[highlight default link stormPath        String]])
vim.cmd([[highlight default link stormBrace       Delimiter]])
vim.cmd([[highlight default link stormBuiltin Special]])
vim.cmd([[highlight default link stormBoolean Boolean]])



vim.b.current_syntax = "storm"
