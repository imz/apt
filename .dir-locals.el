;;; Directory Local Variables
;;; For more information see (info "(emacs) Directory Variables")
((nil
  (c-file-style . "bsd") ;; for indenting braces after "if", "for" etc.
  (c-basic-offset . 3))

 (c++-mode
  ;; Don't mix tabs and spaces (e.g., when indenting to pos 9):
  (indent-tabs-mode . nil))

 ;; For switching the mode in .h headers:
 (c-mode
  (mode . c++)
  (indent-tabs-mode . nil)))
