;;; storm-mode.el --- Major mode for sk's Stormfile DSL -*- lexical-binding: t; -*-

;; Ported from a custom Neovim ftplugin/syntax file for the same DSL.
;; Covers: syntax highlighting (comments, strings, keywords, keys,
;; operators, flags, paths, numbers, braces, builtins, booleans) and
;; a format-on-save indenter matching the original Lua logic.

;;; Syntax table -----------------------------------------------------
;; Handles both `// line comments` and `/* block comments */`, plus
;; both " and ' as string delimiters.

(defvar storm-mode-syntax-table
  (let ((table (make-syntax-table)))
    (modify-syntax-entry ?/  ". 124" table)
    (modify-syntax-entry ?*  ". 23b" table)
    (modify-syntax-entry ?\n ">"     table)
    (modify-syntax-entry ?_  "_"     table)
    (modify-syntax-entry ?\" "\""    table)
    (modify-syntax-entry ?'  "\""    table)
    table)
  "Syntax table for `storm-mode'.")

;;; Faces --------------------------------------------------------------
;; a small dedicated face is defined here. Add a theme entry for it,
;;   `(storm-boolean-face ((t (:foreground ,boolean-fg :weight bold))))

(defface storm-boolean-face
  '((t :inherit font-lock-constant-face))
  "Face for `true'/`false' literals in Stormfiles."
  :group 'storm)

;;; Keywords -------------------------------------------------------------

(defconst storm-keywords
  '("cc" "linker" "sources" "includes" "if" "else" "target" "codegen"
    "cflags" "lflags" "defines" "define" "literal" "mode" "kind"
    "depends" "exclude" "out" "out_dir" "print" "install" "exit"
    "bundle" "deploy" "rpath")
  "Keywords recognized by the Stormfile DSL.")

(defconst storm-booleans '("true" "false"))

;;; Font-lock ------------------------------------------------------------
(defconst storm-font-lock-keywords
  (list
   ;; key:: or key: (identifier immediately before a colon/double-colon)
   '("\\<\\([[:alnum:]_]+\\)\\(::?\\)" (1 'font-lock-variable-name-face) (2 'font-lock-operator-face))

   ;; flags: -foo, -foo-bar, -foo=bar
   '("-[[:alpha:]_][-=[:alnum:]_]*" . 'font-lock-preprocessor-face)

   ;; builtins: __version__ style
   '("__[[:alnum:]_]+__" . 'font-lock-builtin-face)

   ;; numbers
   '("\\<[0-9]+\\>" . 'font-lock-number-face)

   ;; braces
   '("[{}]" . 'font-lock-bracket-face)

   ;; booleans
   (cons (regexp-opt storm-booleans 'words) ''storm-boolean-face)

   (cons (regexp-opt storm-keywords 'words) ''font-lock-keyword-face))
  "Font-lock keyword table for `storm-mode'.")

;;; Format-on-save indenter ----------------------------------------------
(defun storm--format-buffer ()
  "Re-indent the current Stormfile buffer per the sk DSL's indent rules."
  (let* ((indent-unit "    ")
         (orig-line (line-number-at-pos))
         (orig-col (current-column))
         (text (buffer-string))
         (lines (split-string text "\n"))
         (depth 0)
         (prop-indent 0)
         (result '()))

    (when (and (> (length lines) 1) (string-empty-p (car (last lines))))
      (setq lines (butlast lines)))
    (dolist (line lines)
      (let ((stripped (string-trim line)))
        (if (string-empty-p stripped)
            (progn
              (setq prop-indent 0)
              (push "" result))
          (progn
            (cond
             ((string-match-p "\\`}" stripped)
              (setq depth (max 0 (1- depth)))
              (setq prop-indent 0))
             ((or (string-match-p "\\`[[:alnum:]_]+::?" stripped)
                  (string-match-p "\\`if[[:space:]]*(" stripped))
              (setq prop-indent 0)))
            (let ((total-depth (+ depth prop-indent)))
              (push (concat (apply #'concat (make-list total-depth indent-unit))
                            stripped)
                    result))
            (cond
             ((string-match-p "{[[:space:]]*\\'" stripped)
              (setq depth (1+ depth)))
             ((string-match-p "::?[[:space:]]*\\'" stripped)
              (setq prop-indent 1)))))))
    (setq result (nreverse result))
    
    (let ((inhibit-read-only t))
      (erase-buffer)
      (insert (string-join result "\n"))
      (insert "\n")
      (goto-char (point-min))
      (forward-line (1- orig-line))
      (move-to-column orig-col))))

;;; Major mode -------------------------------------------------------------

;;;###autoload
(define-derived-mode storm-mode prog-mode "Storm"
  "Major mode for editing sk Stormfiles."
  :syntax-table storm-mode-syntax-table
  (setq-local font-lock-defaults '(storm-font-lock-keywords))
  (setq-local comment-start "// ")
  (setq-local comment-end "")
  (setq-local tab-width 4)
  (setq-local indent-tabs-mode nil)
  (add-hook 'before-save-hook #'storm--format-buffer nil t))

;;;###autoload
(add-to-list 'auto-mode-alist '("Stormfile\\'" . storm-mode))
;;;###autoload
(add-to-list 'auto-mode-alist '("\\.storm\\'" . storm-mode))

(provide 'storm-mode)

;;; storm-mode.el ends here
