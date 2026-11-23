set tabstop=8
set shiftwidth=3
set softtabstop=3

set cinoptions=L0
set cinoptions+=l1

" use z+o to expand folds
"     z+c to close them
set foldmethod=marker
set foldmarker={{{,}}}

" don't highlight fold markers inside one-line comments
autocmd Syntax c,cpp syn match cCommentFoldMarker "/\*{{{[*}]*\*/" contained containedin=cCommentL
autocmd Syntax c,cpp hi link cCommentFoldMarker cComment
