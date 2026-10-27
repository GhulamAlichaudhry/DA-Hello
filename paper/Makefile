# DA-HELLO IEEE IoT Journal manuscript
# Build: make   (or ./build.sh)
# Requires: pdflatex, bibtex, IEEEtran class + IEEEtran.bst (texlive)

LATEX  ?= pdflatex
BIBTEX ?= bibtex
MAIN   = main

.PHONY: all main clean

all: $(MAIN).pdf

main: $(MAIN).pdf

SECTIONS = authors.tex abstract.tex keywords.tex \
	sec1_introduction.tex sec2_related.tex \
	sec3_system_problem.tex sec4_design.tex \
	sec5_evaluation.tex sec5_conclusion.tex \
	fig_network.tex fig_carriers.tex fig_flow.tex \
	tab1.tex tab2.tex tab3.tex tab4.tex \
	refs.bib \
	fig1_network.pdf fig3_flow.pdf fig4_deploy_top.pdf \
	fig5_pareto.pdf fig6_header.pdf fig7_refresh.pdf fig8_scale.pdf

$(MAIN).pdf: $(MAIN).tex $(SECTIONS)
	$(LATEX) -interaction=nonstopmode $(MAIN)
	-$(BIBTEX) $(MAIN)
	$(LATEX) -interaction=nonstopmode $(MAIN)
	$(LATEX) -interaction=nonstopmode $(MAIN)

clean:
	rm -f $(MAIN).aux $(MAIN).log $(MAIN).out $(MAIN).blg \
		$(MAIN).bbl $(MAIN).pdf
