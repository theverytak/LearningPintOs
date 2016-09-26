" Syntax Highlighting
syntax on

"this makes me comfortable"
colorscheme default

"allow cursor keys
set esckeys

"allow backspace in insert mode
set backspace=indent,eol,start

"auto indent
set autoindent
set cindent

set ignorecase

"show status line always
set laststatus=2

"enable mouse in all modes
set mouse=a

set scrolloff=3

" add the g flag to search/replace by default
set gdefault

"show filename
set title

"line number
set nu

"Use mouse
set mouse=a

"show col row at right bottom
set ruler

"Hightlight search results
set hlsearch

"한글자 입력할 때마다 매치되는 부분 표시하기
set incsearch

"show matching brackets
set showmatch

"set utf8 as standard encoding
set encoding=utf8

"2 spaces == 1tab
set tabstop=2

"auto indent's tab size
set shiftwidth=2

"enable ctags"
set tags=./tags,tags
set tags+=~/pintos/src/tags
set tags+=~/system_prog/lecture_sysprog/02_file_io/tags

"using cscope"
set csprg=/usr/bin/cscope

set csto=0

set cst

set nocsverb

function! AddCscopeDatabase()

let currentdir = getcwd()

while !filereadable("cscope.out") && getcwd() != "/"

cd ..

endwhile

if filereadable("cscope.out")

csc add cscope.out

endif

endfunction

call AddCscopeDatabase()

set csverb




"autocomp"
function! InsertTabWrapper()
	let col = col('.') - 1
	if !col || getline('.')[col-1]!~'\k'
	return "\<TAB>"
	else
	if pumvisible()
	return "\<C-N>"
	else
	return "\<C-N>\<C-P>"
	end
	endif
	endfunction

inoremap <tab> <c-r>=InsertTabWrapper()<cr>

hi Pmenu ctermbg=blue
hi PmenuSel ctermbg=yellow ctermfg=black
hi PmenuSbar ctermbg=blue

