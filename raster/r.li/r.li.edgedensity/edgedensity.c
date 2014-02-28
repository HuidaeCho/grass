/*
 * \brief calculates edge density index
 *
 *  AUTHOR: Serena Pallecchi student of Computer Science University of Pisa (Italy)
 *                      Commission from Faunalia Pontedera (PI) www.faunalia.it
 *
 *   This program is free software under the GPL (>=v2)
 *   Read the COPYING file that comes with GRASS for details.
 *       
 */

#include <stdlib.h>
#include <fcntl.h>		/* for O_RDONLY usage */
#include <math.h>

#include <grass/gis.h>
#include <grass/raster.h>
#include <grass/glocale.h>

#include "../r.li.daemon/avlDefs.h"
#include "../r.li.daemon/avl.h"
#include "../r.li.daemon/daemon.h"

rli_func edgedensity;
int calculate(int fd, struct area_entry *ad, char **valore, double *result);
int calculateD(int fd, struct area_entry *ad, char **valore, double *result);
int calculateF(int fd, struct area_entry *ad, char **valore, double *result);

int main(int argc, char *argv[])
{
    struct Option *raster, *conf, *output, *class;
    struct GModule *module;
    char **par = NULL;

    G_gisinit(argv[0]);
    module = G_define_module();
    module->description =
	_("Calculates edge density index on a raster map, using a 4 neighbour algorithm");
    G_add_keyword(_("raster"));
    G_add_keyword(_("landscape structure analysis"));
    G_add_keyword(_("patch index"));

    /* define options */

    raster = G_define_standard_option(G_OPT_R_INPUT);

    conf = G_define_standard_option(G_OPT_F_INPUT);
    conf->key = "config";
    conf->description = _("Configuration file");
    conf->required = YES;

    output = G_define_standard_option(G_OPT_R_OUTPUT);

    class = G_define_option();
    class->key = "patch_type";
    class->type = TYPE_STRING;
    class->required = NO;
    class->multiple = NO;
    class->label = _("The value of the patch type");
    class->description = _("It can be integer, double or float; "
			   "it will be changed in function of map type");


    if (G_parser(argc, argv))
	exit(EXIT_FAILURE);

    if (class->answer == NULL)
	par = NULL;
    else
	par = &class->answer;


    return calculateIndex(conf->answer, edgedensity, par, raster->answer,
			  output->answer);
}

int edgedensity(int fd, char **valore, struct area_entry *ad, double *result)
{
    struct Cell_head hd;
    int ris = -1;
    double indice = 0;

    Rast_get_cellhd(ad->raster, "", &hd);

    switch (ad->data_type) {
    case CELL_TYPE:
	{
	    ris = calculate(fd, ad, valore, &indice);
	    break;
	}
    case DCELL_TYPE:
	{
	    ris = calculateD(fd, ad, valore, &indice);
	    break;
	}
    case FCELL_TYPE:
	{
	    ris = calculateF(fd, ad, valore, &indice);
	    break;
	}
    default:
	{
	    G_fatal_error("data type unknown");
	    return RLI_ERRORE;
	}
    }
    if (ris != RLI_OK) {
	return RLI_ERRORE;
    }

    *result = indice;

    return RLI_OK;
}


int calculate(int fd, struct area_entry *ad, char **valore, double *result)
{
    double e = 0;
    double somma = 0;
    double area = 0;
    CELL *buf_corr, *buf_sup, *buf_inf, *buf_null;
    CELL prevCell, corrCell, supCell, infCell, nextCell;
    AVL_table *array;
    long tot = 0;
    long zero = 0;
    long m = 0;
    long bordoCorr = 0;
    avl_tree albero = NULL;
    int i, j;
    int mask_fd = -1, *mask_corr, *mask_sup, *mask_inf, *mask_tmp;
    int masked = FALSE;
    int ris;
    generic_cell c1;

    buf_sup = NULL;
    c1.t = CELL_TYPE;

    /* open mask if needed */
    mask_corr = mask_sup = mask_inf = NULL;
    if (ad->mask == 1) {

	if ((mask_fd = open(ad->mask_name, O_RDONLY, 0755)) < 0) {
	    G_fatal_error("Cannot open mask file <%s>", ad->mask_name);
	    return RLI_ERRORE;  /* FIXME: can not return from a fatal error */
	}

	mask_corr = G_malloc(ad->cl * sizeof(int));
	if (mask_corr == NULL) {
	    G_fatal_error("malloc mask_corr failed");
	    return RLI_ERRORE;
	}

	mask_inf = G_malloc(ad->cl * sizeof(int));
	if (mask_inf == NULL) {
	    G_fatal_error("malloc mask_inf failed");
	    return RLI_ERRORE;
	}

	mask_sup = G_malloc(ad->cl * sizeof(int));
	if (mask_sup == NULL) {
	    G_fatal_error("malloc mask_sup failed");
	    return RLI_ERRORE;
	}

	for (i = 0; i < ad->cl; i++) {
	    mask_corr[i] = 0;
	    mask_sup[i] = 0;
	}

	if (read(mask_fd, mask_inf, (ad->cl * sizeof(int))) < 0) {
	    G_fatal_error("reading mask_inf at first row");
	    return RLI_ERRORE;
	}

	masked = TRUE;
    }

    buf_null = Rast_allocate_c_buf();
    if (buf_null == NULL) {
	G_fatal_error("malloc buf_null failed");
	return RLI_ERRORE;
    }

    /* the first time buf_sup is all null */
    Rast_set_c_null_value(buf_null, Rast_window_cols());
    buf_sup = buf_null;
    buf_inf = buf_null;

    /* for each raster row */
    for (j = 0; j < ad->rl; j++) {

	/* read row of raster */
	buf_corr = RLI_get_cell_raster_row(fd, j + ad->y, ad);

	if (j > 0)		/* not first row */
	    buf_sup = RLI_get_cell_raster_row(fd, j - 1 + ad->y, ad);

	if ((j + 1) < ad->rl) {	/* not last row */
	    buf_inf = RLI_get_cell_raster_row(fd, 1 + j + ad->y, ad);
	}
	else {
	    buf_inf = buf_null;
	}

	/* read mask if needed */
	if (masked) {
	    mask_tmp = mask_sup;
	    mask_sup = mask_corr;
	    mask_corr = mask_inf;
	    mask_inf = mask_tmp;

	    if ((j + 1) < ad->rl) {	/* not last row */
		if (read(mask_fd, mask_inf, (ad->cl * sizeof(int))) < 0) {
		    G_fatal_error("reading mask_inf at row j");
		    return RLI_ERRORE;
		}
	    }
	    else {
		int z = 0;

		for (z = 0; z < ad->cl; z++)
		    mask_inf[z] = 0;
	    }
	}

	Rast_set_c_null_value(&nextCell, 1);
	Rast_set_c_null_value(&prevCell, 1);
	Rast_set_c_null_value(&corrCell, 1);

	for (i = 0; i < ad->cl; i++) {
	    corrCell = buf_corr[i + ad->x];

	    if (masked && mask_corr[i] == 0) {
		Rast_set_c_null_value(&corrCell, 1);
	    }

	    if (!(Rast_is_null_value(&corrCell, CELL_TYPE))) {
		area++;
		if ((i + 1) == ad->cl)	/*last cell of the row */
		    Rast_set_c_null_value(&nextCell, 1);
		else if (masked && mask_corr[i + 1] == 0)
		    Rast_set_c_null_value(&nextCell, 1);
		else
		    nextCell = buf_corr[i + 1 + ad->x];

		if (masked && mask_sup[i] == 0)
		    Rast_set_c_null_value(&supCell, 1);
		else
		    supCell = buf_sup[i + ad->x];

		if (masked && mask_inf[i] == 0)
		    Rast_set_c_null_value(&infCell, 1);
		else
		    infCell = buf_inf[i + ad->x];

		/* calculate how many edges the cell has */
		if ((Rast_is_null_value(&prevCell, CELL_TYPE)) ||
		    (corrCell != prevCell)) {
		    bordoCorr++;
		}

		if ((Rast_is_null_value(&supCell, CELL_TYPE)) ||
		    (corrCell != supCell)) {
		    bordoCorr++;
		}

		if ((Rast_is_null_value(&infCell, CELL_TYPE)) ||
		    (corrCell != infCell)) {
		    bordoCorr++;
		}

		if ((Rast_is_null_value(&nextCell, CELL_TYPE)) ||
		    (corrCell != nextCell)) {
		    bordoCorr++;
		}

		/* store the result in the tree */
		if (albero == NULL) {
		    c1.val.c = corrCell;
		    albero = avl_make(c1, bordoCorr);
		    if (albero == NULL) {
			G_fatal_error("avl_make error");
			return RLI_ERRORE;
		    }
		    m++;
		}
		else {
		    c1.val.c = corrCell;
		    ris = avl_add(&albero, c1, bordoCorr);

		    switch (ris) {
		    case AVL_ERR:
			{
			    G_fatal_error("avl_add error");
			    return RLI_ERRORE;
			}
		    case AVL_ADD:
			{
			    m++;
			    break;
			}
		    case AVL_PRES:
			{
			    break;
			}
		    default:
			{
			    G_fatal_error("avl_add unknown error");
			    return RLI_ERRORE;
			}
		    }
		}
		bordoCorr = 0;
	    }
	    prevCell = corrCell;
	}
    }

    /* calculate index */
    if (area > 0) {
	if (valore != NULL) {	/* only 1 class */
	    char *sval;
	    int val;
	    CELL cella;

	    sval = valore[0];
	    val = atoi(sval);
	    cella = val;
	    c1.t = CELL_TYPE;
	    c1.val.c = cella;
	    e = (double)howManyCell(albero, c1);
	    somma = e;
	}
	else {			/* all classes */
	    array = G_malloc(m * sizeof(AVL_tableRow));
	    if (array == NULL) {
		G_fatal_error("malloc array failed");
		return RLI_ERRORE;
	    }
	    tot = avl_to_array(albero, zero, array);
	    if (tot != m) {
		G_warning
		    ("avl_to_array unexpected value. the result could be wrong");
	    }
	    for (i = 0; i < m; i++) {
		e = (double)array[i]->tot;
		somma = somma + e;
	    }
	    G_free(array);
	}

	*result = somma * 10000 / area;
    }
    else
	Rast_set_d_null_value(result, 1);

    if (masked) {
	close(mask_fd);
	G_free(mask_inf);
	G_free(mask_corr);
	G_free(mask_sup);
    }

    G_free(buf_null);
    avl_destroy(albero);

    return RLI_OK;
}

int calculateD(int fd, struct area_entry *ad, char **valore, double *result)
{
    double e = 0;
    double somma = 0;
    double area = 0;

    DCELL *buf_corr, *buf_sup, *buf_inf, *buf_null;
    DCELL prevCell, corrCell, supCell, infCell, nextCell;

    AVL_table *array;

    long tot = 0;
    long zero = 0;
    long m = 0;
    long bordoCorr = 0;

    avl_tree albero = NULL;

    int i, j;
    int mask_fd = -1, *mask_corr, *mask_sup, *mask_inf, *mask_tmp;
    int masked = FALSE;
    int ris;

    generic_cell c1;

    c1.t = DCELL_TYPE;

    /* open mask if needed */
    mask_corr = mask_sup = mask_inf = NULL;
    if (ad->mask == 1) {
	if ((mask_fd = open(ad->mask_name, O_RDONLY, 0755)) < 0) {
	    G_fatal_error("Cannot open mask file");
	    return RLI_ERRORE;  /* FIXME: can not return from a fatal error */
	}

	mask_corr = G_malloc(ad->cl * sizeof(int));
	if (mask_corr == NULL) {
	    G_fatal_error("malloc mask_corr failed");
	    return RLI_ERRORE;
	}

	mask_inf = G_malloc(ad->cl * sizeof(int));
	if (mask_inf == NULL) {
	    G_fatal_error("malloc mask_inf failed");
	    return RLI_ERRORE;
	}

	mask_sup = G_malloc(ad->cl * sizeof(int));
	if (mask_sup == NULL) {
	    G_fatal_error("malloc mask_sup failed");
	    return RLI_ERRORE;
	}

	for (i = 0; i < ad->cl; i++) {
	    mask_corr[i] = 0;
	    mask_sup[i] = 0;
	}

	if (read(mask_fd, mask_inf, (ad->cl * sizeof(int))) <= 0) {
	    G_fatal_error("reading mask_corr");
	    return RLI_ERRORE;
	}

	masked = TRUE;
    }

    buf_null = Rast_allocate_d_buf();
    if (buf_null == NULL) {
	G_fatal_error("malloc buf_null failed");
	return RLI_ERRORE;
    }

    /* the first time buf_sup is all null */
    Rast_set_d_null_value(buf_null, Rast_window_cols());
    buf_sup = buf_null;
    buf_inf = buf_null;

    /* for each raster row */
    for (j = 0; j < ad->rl; j++) {
	/* read row of raster */
	buf_corr = RLI_get_dcell_raster_row(fd, j + ad->y, ad);

	/* not first row */
	if (j > 0)
	    buf_sup = RLI_get_dcell_raster_row(fd, j - 1 + ad->y, ad);

	if ((j + 1) < ad->rl) {	/* not last row */
	    buf_inf = RLI_get_dcell_raster_row(fd, 1 + j + ad->y, ad);
	}
	else {
	    buf_inf = buf_null;
	}

	/* read mask if needed */
	if (masked) {

	    mask_tmp = mask_sup;
	    mask_sup = mask_corr;
	    mask_corr = mask_inf;
	    mask_inf = mask_tmp;

	    if ((j + 1) < ad->rl) {	/* not last row */
		if (read(mask_fd, mask_inf, (ad->cl * sizeof(int))) < 0) {
		    G_fatal_error("reading mask_inf");
		    return RLI_ERRORE;
		}
	    }
	    else {
		int z = 0;

		for (z = 0; z < ad->cl; z++)
		    mask_inf[z] = 0;
	    }
	}

	Rast_set_d_null_value(&nextCell, 1);
	Rast_set_d_null_value(&prevCell, 1);
	Rast_set_d_null_value(&corrCell, 1);

	for (i = 0; i < ad->cl; i++) {	/* for each cell in the row */
	    corrCell = buf_corr[i + ad->x];

	    if (masked && mask_corr[i] == 0) {
		Rast_set_d_null_value(&corrCell, 1);
	    }

	    if (!(Rast_is_null_value(&corrCell, DCELL_TYPE))) {
		area++;
		if ((i + 1) == ad->cl)	/*last cell of the row */
		    Rast_set_d_null_value(&nextCell, 1);
		else if (masked && mask_corr[i + 1] == 0)
		    Rast_set_d_null_value(&nextCell, 1);
		else
		    nextCell = buf_corr[i + 1 + ad->x];

		if (masked && mask_sup[i] == 0)
		    Rast_set_d_null_value(&supCell, 1);
		else
		    supCell = buf_sup[i + ad->x];

		if (masked && mask_inf[i] == 0)
		    Rast_set_d_null_value(&infCell, 1);
		else
		    infCell = buf_inf[i + ad->x];

		/* calculate how many edges the cell has */

		if ((Rast_is_null_value(&prevCell, DCELL_TYPE)) ||
		    (corrCell != prevCell)) {
		    bordoCorr++;
		}

		if ((Rast_is_null_value(&supCell, DCELL_TYPE)) ||
		    (corrCell != supCell)) {
		    bordoCorr++;
		}

		if ((Rast_is_null_value(&infCell, DCELL_TYPE)) ||
		    (corrCell != infCell)) {
		    bordoCorr++;
		}

		if ((Rast_is_null_value(&nextCell, DCELL_TYPE)) ||
		    (corrCell != nextCell)) {
		    bordoCorr++;
		}

		/* store the result in the tree */
		if (albero == NULL) {
		    c1.val.dc = corrCell;
		    albero = avl_make(c1, bordoCorr);
		    if (albero == NULL) {
			G_fatal_error("avl_make error");
			return RLI_ERRORE;
		    }
		    m++;
		}
		else {
		    c1.val.dc = corrCell;
		    ris = avl_add(&albero, c1, bordoCorr);

		    switch (ris) {
		    case AVL_ERR:
			{
			    G_fatal_error("avl_add error");
			    return RLI_ERRORE;
			}
		    case AVL_ADD:
			{
			    m++;
			    break;
			}
		    case AVL_PRES:
			{
			    break;
			}
		    default:
			{
			    G_fatal_error("avl_add unknown error");
			    return RLI_ERRORE;
			}
		    }
		}
		bordoCorr = 0;
	    }
	    prevCell = corrCell;
	}
    }

    /* calculate index */
    if (area > 0) {
	if (valore != NULL) {	/* only 1 class */
	    char *sval;
	    double val;
	    DCELL cella;

	    sval = valore[0];
	    val = (double)atof(sval);
	    cella = val;
	    c1.val.dc = cella;
	    c1.t = DCELL_TYPE;
	    e = (double)howManyCell(albero, c1);
	    somma = e;
	}
	else {			/* all classes */
	    array = G_malloc(m * sizeof(AVL_tableRow));
	    if (array == NULL) {
		G_fatal_error("malloc array failed");
		return RLI_ERRORE;
	    }
	    tot = avl_to_array(albero, zero, array);
	    if (tot != m) {
		G_warning
		    ("avl_to_array unexpected value. the result could be wrong");
	    }
	    for (i = 0; i < m; i++) {
		e = (double)array[i]->tot;
		somma = somma + e;
	    }
	    G_free(array);
	}
	*result = somma * 10000 / area;
    }
    else
	Rast_set_d_null_value(result, 1);

    if (masked) {
	close(mask_fd);
	G_free(mask_inf);
	G_free(mask_corr);
	G_free(mask_sup);
    }

    G_free(buf_null);
    avl_destroy(albero);

    return RLI_OK;
}

int calculateF(int fd, struct area_entry *ad, char **valore, double *result)
{
    double e = 0;
    double somma = 0;
    double area = 0;

    FCELL *buf_corr, *buf_sup, *buf_inf, *buf_null;
    FCELL prevCell, corrCell, supCell, infCell, nextCell;

    AVL_table *array;

    long tot = 0;
    long zero = 0;
    long m = 0;
    long bordoCorr = 0;

    avl_tree albero = NULL;

    int i, j;
    int mask_fd = -1, *mask_corr, *mask_sup, *mask_inf, *mask_tmp;
    int masked = FALSE;
    int ris;

    generic_cell c1;

    c1.t = FCELL_TYPE;
    /* open mask if needed */
    mask_corr = mask_sup = mask_inf = NULL;
    if (ad->mask == 1) {
	if ((mask_fd = open(ad->mask_name, O_RDONLY, 0755)) < 0) {
	    G_fatal_error("Cannot open mask file");
	    return RLI_ERRORE;  /* FIXME: can not return from a fatal error */
	}

	mask_corr = G_malloc(ad->cl * sizeof(int));
	if (mask_corr == NULL) {
	    G_fatal_error("malloc mask_corr failed");
	    return RLI_ERRORE;
	}

	mask_inf = G_malloc(ad->cl * sizeof(int));
	if (mask_inf == NULL) {
	    G_fatal_error("malloc mask_inf failed");
	    return RLI_ERRORE;
	}

	mask_sup = G_malloc(ad->cl * sizeof(int));
	if (mask_sup == NULL) {
	    G_fatal_error("malloc mask_sup failed");
	    return RLI_ERRORE;
	}

	for (i = 0; i < ad->cl; i++) {
	    mask_corr[i] = 0;
	    mask_sup[i] = 0;
	}

	if (read(mask_fd, mask_inf, (ad->cl * sizeof(int))) <= 0) {
	    G_fatal_error("reading mask_corr");
	    return RLI_ERRORE;
	}

	masked = TRUE;
    }

    buf_null = Rast_allocate_f_buf();
    if (buf_null == NULL) {
	G_fatal_error("malloc buf_null failed");
	return RLI_ERRORE;
    }

    /* the first time buf_sup is all null */
    Rast_set_f_null_value(buf_null, Rast_window_cols());
    buf_sup = buf_null;
    buf_inf = buf_null;

    /* for each raster row */
    for (j = 0; j < ad->rl; j++) {
	/* read row of raster */
	buf_corr = RLI_get_fcell_raster_row(fd, j + ad->y, ad);

	if (j > 0)		/* not first row */
	    buf_sup = RLI_get_fcell_raster_row(fd, j - 1 + ad->y, ad);

	if ((j + 1) < ad->rl) {	/* not last row */
	    buf_inf = RLI_get_fcell_raster_row(fd, 1 + j + ad->y, ad);
	}
	else {
	    buf_inf = buf_null;
	}

	/* read mask if needed */
	if (masked) {
	    mask_tmp = mask_sup;
	    mask_sup = mask_corr;
	    mask_corr = mask_inf;
	    mask_inf = mask_tmp;

	    if ((j + 1) < ad->rl) {	/* not last row */
		if (read(mask_fd, mask_inf, (ad->cl * sizeof(int))) < 0) {
		    G_fatal_error("reading mask_inf");
		    return RLI_ERRORE;
		}
	    }
	    else {
		int z = 0;

		for (z = 0; z < ad->cl; z++)
		    mask_inf[z] = 0;
	    }
	}

	Rast_set_f_null_value(&nextCell, 1);
	Rast_set_f_null_value(&prevCell, 1);
	Rast_set_f_null_value(&corrCell, 1);

	for (i = 0; i < ad->cl; i++) {	/* for each cell in the row */
	    corrCell = buf_corr[i + ad->x];

	    if (masked && mask_corr[i] == 0) {
		Rast_set_f_null_value(&corrCell, 1);
	    }

	    if (!(Rast_is_null_value(&corrCell, FCELL_TYPE))) {
		area++;
		if ((i + 1) == ad->cl)	/*last cell of the row */
		    Rast_set_f_null_value(&nextCell, 1);
		else if (masked && mask_corr[i + 1] == 0)
		    Rast_set_f_null_value(&nextCell, 1);
		else
		    nextCell = buf_corr[i + 1 + ad->x];

		if (masked && mask_sup[i] == 0)
		    Rast_set_f_null_value(&supCell, 1);
		else
		    supCell = buf_sup[i + ad->x];

		if (masked && mask_inf[i] == 0)
		    Rast_set_f_null_value(&infCell, 1);
		else
		    infCell = buf_inf[i + ad->x];

		/* calculate how many edges the cell has */
		if ((Rast_is_null_value(&prevCell, FCELL_TYPE)) ||
		    (corrCell != prevCell)) {
		    bordoCorr++;
		}

		if ((Rast_is_null_value(&supCell, FCELL_TYPE)) ||
		    (corrCell != supCell)) {
		    bordoCorr++;
		}

		if ((Rast_is_null_value(&infCell, FCELL_TYPE)) ||
		    (corrCell != infCell)) {
		    bordoCorr++;
		}

		if ((Rast_is_null_value(&nextCell, FCELL_TYPE)) ||
		    (corrCell != nextCell)) {
		    bordoCorr++;
		}

		/* store the result in the tree */
		if (albero == NULL) {
		    c1.val.fc = corrCell;
		    albero = avl_make(c1, bordoCorr);
		    if (albero == NULL) {
			G_fatal_error("avl_make error");
			return RLI_ERRORE;
		    }
		    m++;
		}
		else {
		    c1.val.fc = corrCell;
		    ris = avl_add(&albero, c1, bordoCorr);

		    switch (ris) {
		    case AVL_ERR:
			{
			    G_fatal_error("avl_add error");
			    return RLI_ERRORE;
			}
		    case AVL_ADD:
			{
			    m++;
			    break;
			}
		    case AVL_PRES:
			{
			    break;
			}
		    default:
			{
			    G_fatal_error("avl_add unknown error");
			    return RLI_ERRORE;
			}
		    }
		}
		bordoCorr = 0;
	    }
	    prevCell = corrCell;
	}
    }

    /* calculate index */
    if (area > 0) {
	if (valore != NULL) {	/* only 1 class */
	    char *sval;
	    float val;
	    FCELL cella;

	    sval = valore[0];
	    val = (float)atof(sval);
	    cella = val;
	    c1.t = FCELL_TYPE;
	    c1.val.fc = cella;
	    e = (double)howManyCell(albero, c1);
	    somma = e;

	}
	else {			/* all classes */
	    array = G_malloc(m * sizeof(AVL_tableRow));
	    if (array == NULL) {
		G_fatal_error("malloc array failed");
		return RLI_ERRORE;
	    }
	    tot = avl_to_array(albero, zero, array);
	    if (tot != m) {
		G_warning
		    ("avl_to_array unexpected value. the result could be wrong");
	    }
	    for (i = 0; i < m; i++) {
		e = (double)array[i]->tot;
		somma = somma + e;
	    }
	    G_free(array);
	}
	*result = somma * 10000 / area;
    }
    else
	Rast_set_d_null_value(result, 1);

    if (masked) {
	close(mask_fd);
	G_free(mask_inf);
	G_free(mask_corr);
	G_free(mask_sup);
    }

    G_free(buf_null);
    avl_destroy(albero);

    return RLI_OK;
}
