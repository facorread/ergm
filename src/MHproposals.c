/*  File src/MHproposals.c in package ergm, part of the Statnet suite
 *  of packages for network analysis, https://statnet.org .
 *
 *  This software is distributed under the GPL-3 license.  It is free,
 *  open source, and has the attribution requirements (GPL Section 7) at
 *  https://statnet.org/attribution
 *
 *  Copyright 2003-2020 Statnet Commons
 */
#include "MHproposals.h"
#include "ergm_edgelist.h"
#include "ergm_changestat.h"
#include "ergm_rlebdm.h"
#include "ergm_MHstorage.h"
#include "ergm_unsorted_edgelist.h"
#include "ergm_weighted_population.h"
#include "ergm_dyadgen.h"
#include "ergm_Rutil.h"

/*********************
 void MH_randomtoggle

 Default MH algorithm
*********************/
MH_P_FN(MH_randomtoggle){  

  /* *** don't forget tail-> head now */

  if(MHp->ntoggles == 0) { /* Initialize randomtoggle */
    MH_STORAGE = DyadGenInitializeR(MHp->R, nwp, FALSE);
    MHp->ntoggles=1;
    return;
  }
  
  BD_LOOP({
      DyadGenRandDyad(Mtail, Mhead, MH_STORAGE);
    });
}

MH_F_FN(Mf_randomtoggle){
  DyadGenDestroy(MH_STORAGE);
  MH_STORAGE = NULL;
}

/********************
   void MH_TNT
   Propose ONLY edges on a static list
   Use TNT weights.
   This is a fusion of MH_DissolutionMLETNT and MH_TNT:

   A "intersect" network is constructed that is the intersection of
   dyads on the static list and the edges present in nwp. Then,
   standard TNT procedure is followed, but the dyad space (and the
   number of dyads) is the number of dyads in the static list and the
   network for the ties is the ties in the discord network.
***********************/

MH_P_FN(Mp_TNT){
  if(MHp->ntoggles == 0) { /* Initialize randomtoggle */
    MH_STORAGE = DyadGenInitializeR(MHp->R, nwp, TRUE);
    MHp->ntoggles=1;
    return;
  }

  const double P=0.5, Q=1-P;
  double DP = P*((DyadGen *)MH_STORAGE)->ndyads, DO = DP/Q;

  Edge nedges = DyadGenEdgecount(MH_STORAGE);
  double logratio=0;
  BD_LOOP({
      if (unif_rand() < P && nedges > 0) { /* Select a tie at random from the network of eligibles */
        DyadGenRandEdge(Mtail, Mhead, MH_STORAGE);
	logratio = TNT_LR_E(nedges, Q, DP, DO);
      }else{ /* Select a dyad at random from the list */
	DyadGenRandDyad(Mtail, Mhead, MH_STORAGE);
	
	if(IS_OUTEDGE(Mtail[0],Mhead[0])){
	  logratio = TNT_LR_DE(nedges, Q, DP, DO);
	}else{
	  logratio = TNT_LR_DN(nedges, Q, DP, DO);
	}
      }
    });
  MHp->logratio += logratio;
}

MH_F_FN(Mf_TNT){
  DyadGenDestroy(MH_STORAGE);
  MH_STORAGE = NULL;
}

/********************
    MH_BDStratTNT
********************/

typedef struct {
  UnsrtEL **els;
  Vertex ***nodesvec;
  int **attrcounts;
  
  int strattailtype;
  int bdtailtype;
  int tailindex;
  int tailmaxl;
  
  int stratheadtype;
  int bdheadtype;  
  int headindex;
  int headmaxl;
  
  int stratmixingtype;
  
  double currentcumprob;
  double proposedcumprob;
  
  double *originalprobvec;
  double *currentprobvec;
  
  WtPop *wtp;
  
  int bound;
  int nmixtypes;
  
  int *strat_vattr;
  int *bd_vattr;
  
  int *BDtypesbyStrattype;
  int **BDtailsbyStrattype;
  int **BDheadsbyStrattype;
  
  int *strattailtypes;
  int *stratheadtypes;
  
  int nstratlevels;
  
  int *currentsubmaxledgestype;
  int **indmat;
} BDStratTNTStorage;

MH_I_FN(Mi_BDStratTNT) {
  // process the inputs and initialize all the edgelists in storage; set MHp->ntoggles to 1
  MHp->ntoggles = 1;

  ALLOC_STORAGE(1, BDStratTNTStorage, sto);
  
  int nmixtypes = asInteger(getListElement(MHp->R, "nmixtypes"));
  
  int *strattailattrs = INTEGER(getListElement(MHp->R, "strattailattrs"));
  int *stratheadattrs = INTEGER(getListElement(MHp->R, "stratheadattrs"));

  double *probvec = REAL(getListElement(MHp->R, "probvec"));
    
  int nattrcodes = asInteger(getListElement(MHp->R, "nattrcodes"));
  sto->nstratlevels = nattrcodes;
  
  int *strat_vattr = INTEGER(getListElement(MHp->R, "strat_vattr"));
  
  UnsrtEL **els = (UnsrtEL **)Calloc(nmixtypes, UnsrtEL *);
      
  for(int i = 0; i < nmixtypes; i++) {
    els[i] = UnsrtELInitialize(0, NULL, NULL, FALSE);
  }
  
  int *inputindmat = INTEGER(getListElement(MHp->R, "indmat"));  
  
  int **indmat = (int **)Calloc(nattrcodes, int *);
  indmat[0] = inputindmat;
  for(int i = 1; i < nattrcodes; i++) {
    indmat[i] = indmat[i - 1] + nattrcodes;
  }
  
  sto->currentsubmaxledgestype = Calloc(nmixtypes, int);
  
  int bound = asInteger(getListElement(MHp->R, "bound"));
  
  Vertex head;
  Edge e;
  for(Vertex tail = 1; tail <= N_NODES; tail++) {
    STEP_THROUGH_OUTEDGES(tail, e, head) {
      int index = indmat[(int)strat_vattr[tail - 1]][(int)strat_vattr[head - 1]];
      if(index >= 0) {
        UnsrtELInsert(tail, head, els[index]);
        if(IN_DEG[tail] + OUT_DEG[tail] < bound && IN_DEG[head] + OUT_DEG[head] < bound) {
          sto->currentsubmaxledgestype[index]++;
        }
      }
    }
  }
  sto->indmat = indmat;
  
  // above handles initialization of edgelists according to the "strat" part of BDStratTNT
    
  int bdlevels = asInteger(getListElement(MHp->R, "bd_levels"));
  
  int *bd_vattr = INTEGER(getListElement(MHp->R, "bd_vattr"));
  
  int *nodecountsbypairedcode = INTEGER(getListElement(MHp->R, "nodecountsbypairedcode"));
  
  
  int **nodecountsmat = (int **)Calloc(nattrcodes, int *);
  nodecountsmat[0] = nodecountsbypairedcode;
  for(int i = 1; i < nattrcodes; i++) {
    nodecountsmat[i] = nodecountsmat[i - 1] + bdlevels;
  }

  Vertex ***nodesvec = (Vertex ***)Calloc(nattrcodes, Vertex **);
  for(int i = 0; i < nattrcodes; i++) {
    nodesvec[i] = (Vertex **)Calloc(bdlevels, Vertex *);
    for(int j = 0; j < bdlevels; j++) {
      nodesvec[i][j] = (Vertex *)Calloc(nodecountsmat[i][j], Vertex);
    }
  }
  Free(nodecountsmat);


  int **attrcounts = (int **)Calloc(nattrcodes, int *);
  for(int i = 0; i < nattrcodes; i++) {
    attrcounts[i] = (int *)Calloc(bdlevels, int);
  }
  
  for(Vertex vertex = 1; vertex <= N_NODES; vertex++) {
    if(IN_DEG[vertex] + OUT_DEG[vertex] < bound) {
      // add vertex to the submaximal list corresponding to its attribute type
      nodesvec[(int)strat_vattr[vertex - 1]][(int)bd_vattr[vertex - 1]][attrcounts[(int)strat_vattr[vertex - 1]][(int)bd_vattr[vertex - 1]]] = vertex;
      attrcounts[(int)strat_vattr[vertex - 1]][(int)bd_vattr[vertex - 1]]++;
    }
  }

  sto->BDtypesbyStrattype = INTEGER(getListElement(MHp->R, "BDtypesbyStrattype"));
  
  sto->BDtailsbyStrattype = Calloc(nmixtypes, int *);
  sto->BDheadsbyStrattype = Calloc(nmixtypes, int *);
  
  sto->BDtailsbyStrattype[0] = INTEGER(getListElement(MHp->R, "BDtailsbyStrattype"));
  sto->BDheadsbyStrattype[0] = INTEGER(getListElement(MHp->R, "BDheadsbyStrattype"));
  
  for(int i = 1; i < nmixtypes; i++) {
    sto->BDtailsbyStrattype[i] = sto->BDtailsbyStrattype[i - 1] + (int)sto->BDtypesbyStrattype[i - 1];
    sto->BDheadsbyStrattype[i] = sto->BDheadsbyStrattype[i - 1] + (int)sto->BDtypesbyStrattype[i - 1];
  }
  
  int empirical_flag = asInteger(getListElement(MHp->R, "empirical_flag"));
  sto->originalprobvec = Calloc(nmixtypes, double);
  if(empirical_flag) {
    int sumedges = 0;
    for(int i = 0; i < nmixtypes; i++) {
      sto->originalprobvec[i] = els[i]->nedges;
      sumedges += els[i]->nedges;
    }
    
    // empirical_flag with no edges is an error
    if(sumedges == 0) {
      MHp->ntoggles = MH_FAILED;
      return;
    }
    
    for(int i = 0; i < nmixtypes; i++) {
      sto->originalprobvec[i] /= sumedges;
    }
  } else {
    memcpy(sto->originalprobvec, probvec, nmixtypes*sizeof(double));
  }
  
  Dyad sumcurrentdyads = 0;
  
  double *currentprobvec = Calloc(nmixtypes, double);
  double sumprobs = 0;
  
  for(int i = 0; i < nmixtypes; i++) {
    Dyad currentdyads = 0;
    for(int j = 0; j < (int)sto->BDtypesbyStrattype[i]; j++) {
      int tailcounts = attrcounts[(int)strattailattrs[i]][(int)sto->BDtailsbyStrattype[i][j]];
      int headcounts = attrcounts[(int)stratheadattrs[i]][(int)sto->BDheadsbyStrattype[i][j]];
      
      if(strattailattrs[i] != stratheadattrs[i] || sto->BDtailsbyStrattype[i][j] != sto->BDheadsbyStrattype[i][j]) {
        currentdyads += (Dyad)tailcounts*headcounts;
      } else {
        currentdyads += (Dyad)tailcounts*(headcounts - 1)/2;
      }
    }
    
    sumcurrentdyads += currentdyads;
    
    if(currentdyads > 0 || els[i]->nedges > 0) {
      currentprobvec[i] = sto->originalprobvec[i];
      sumprobs += sto->originalprobvec[i];
    } // else it's already 0
  }
  
  // if we cannot toggle any edges or dyads, error
  if(EDGECOUNT(nwp) == 0 && sumcurrentdyads == 0) {
    MHp->ntoggles = MH_FAILED;
    return;
  }
  
  sto->els = els;
  sto->nodesvec = nodesvec;
  sto->attrcounts = attrcounts;
    
  sto->currentprobvec = currentprobvec;
  
  sto->currentcumprob = sumprobs;
  
  sto->proposedcumprob = 1;
    
  sto->bound = bound;
  sto->nmixtypes = nmixtypes;
  
  sto->strat_vattr = strat_vattr;
  sto->bd_vattr = bd_vattr;
    
  sto->strattailtypes = strattailattrs;
  sto->stratheadtypes = stratheadattrs;
  
  sto->wtp = WtPopInitialize(sto->nmixtypes, sto->currentprobvec);
}

MH_P_FN(MH_BDStratTNT) {
  GET_STORAGE(BDStratTNTStorage, sto);

  // sample a toggleable strat mixing type on which to make a proposal
  int strat_i = WtPopGetRand(sto->wtp);

  // record the mixing type of the toggle, in case it's needed in the U function later
  sto->stratmixingtype = strat_i;    

  int strattailtype = sto->strattailtypes[strat_i];
  int stratheadtype = sto->stratheadtypes[strat_i];
    
  // number of edges of this mixing type
  int nedgestype = sto->els[strat_i]->nedges;
  
  Dyad ndyadstype = 0;
  for(int j = 0; j < (int)sto->BDtypesbyStrattype[strat_i]; j++) {
    if(strattailtype == stratheadtype && sto->BDtailsbyStrattype[strat_i][j] == sto->BDheadsbyStrattype[strat_i][j]) {
      ndyadstype += (Dyad)sto->attrcounts[strattailtype][(int)sto->BDtailsbyStrattype[strat_i][j]]*(sto->attrcounts[stratheadtype][(int)sto->BDheadsbyStrattype[strat_i][j]] - 1)/2;
    } else {
      ndyadstype += (Dyad)sto->attrcounts[strattailtype][(int)sto->BDtailsbyStrattype[strat_i][j]]*sto->attrcounts[stratheadtype][(int)sto->BDheadsbyStrattype[strat_i][j]];
    }
  }
  
  int edgeflag;
  
  if((unif_rand() < 0.5 && nedgestype > 0) || ndyadstype == 0) {
    // propose toggling off an existing edge of strat mixing type strat_i
    UnsrtELGetRand(Mtail, Mhead, sto->els[strat_i]);    
    edgeflag = TRUE;
  } else {
    // select a random BD toggleable dyad of strat mixing type strat_i and propose toggling it
    Dyad dyadindex = 2*ndyadstype*unif_rand();

    Vertex head;
    Vertex tail;
    
    // this rather ugly block of code is just finding the dyad that corresponds
    // to the dyadindex we drew above, and then setting the info for
    // tail and head appropriately
    for(int j = 0; j < (int)sto->BDtypesbyStrattype[strat_i]; j++) {
      Dyad dyadsthistype;

      int tailcounts = sto->attrcounts[strattailtype][(int)sto->BDtailsbyStrattype[strat_i][j]];
      int headcounts = sto->attrcounts[stratheadtype][(int)sto->BDheadsbyStrattype[strat_i][j]];

      if(strattailtype != stratheadtype || sto->BDtailsbyStrattype[strat_i][j] != sto->BDheadsbyStrattype[strat_i][j]) {
        dyadsthistype = (Dyad)tailcounts*headcounts;
      } else {
        dyadsthistype = (Dyad)tailcounts*(headcounts - 1)/2;
      }
      
      if(dyadindex < 2*dyadsthistype) {
        int tailindex;
        int headindex;
        
        if(strattailtype == stratheadtype && sto->BDtailsbyStrattype[strat_i][j] == sto->BDheadsbyStrattype[strat_i][j]) {
          tailindex = dyadindex / tailcounts;
          headindex = dyadindex % (headcounts - 1);
          if(tailindex == headindex) {
            headindex = headcounts - 1;
          }
                    
          tail = sto->nodesvec[strattailtype][(int)sto->BDtailsbyStrattype[strat_i][j]][tailindex];
          head = sto->nodesvec[stratheadtype][(int)sto->BDheadsbyStrattype[strat_i][j]][headindex];
        } else {
          dyadindex /= 2;
          tailindex = dyadindex / headcounts;
          headindex = dyadindex % headcounts;
          
          tail = sto->nodesvec[strattailtype][(int)sto->BDtailsbyStrattype[strat_i][j]][tailindex];
          head = sto->nodesvec[stratheadtype][(int)sto->BDheadsbyStrattype[strat_i][j]][headindex];
        }
            
        if(tail > head) {
          sto->strattailtype = stratheadtype;
          sto->bdtailtype = sto->BDheadsbyStrattype[strat_i][j];
          sto->stratheadtype = strattailtype;
          sto->bdheadtype = sto->BDtailsbyStrattype[strat_i][j];
          sto->tailindex = headindex;
          sto->headindex = tailindex;
          Mtail[0] = head;
          Mhead[0] = tail;
        } else {
          sto->strattailtype = strattailtype;
          sto->bdtailtype = sto->BDtailsbyStrattype[strat_i][j];
          sto->stratheadtype = stratheadtype;
          sto->bdheadtype = sto->BDheadsbyStrattype[strat_i][j];
          sto->tailindex = tailindex;
          sto->headindex = headindex;
          Mtail[0] = tail;
          Mhead[0] = head;
        }
        
        break;
      } else {
        dyadindex -= 2*dyadsthistype;
      }
    }
       
    // now check if the dyad we drew is already an edge or not
    if(IS_OUTEDGE(Mtail[0],Mhead[0])) {
      // must resample to know edge index; we will fix strat and bd types below;
      // indices within submaximal lists will not be used in the U_FN since this is an off-toggle,
      // so if they're wrong, that's fine
      UnsrtELGetRand(Mtail, Mhead, sto->els[strat_i]);
      edgeflag = TRUE;
    } else {
      edgeflag = FALSE;
    }
  }

  if(edgeflag) {
    sto->strattailtype = sto->strat_vattr[Mtail[0] - 1];
    sto->stratheadtype = sto->strat_vattr[Mhead[0] - 1];
    
    sto->bdtailtype = sto->bd_vattr[Mtail[0] - 1];
    sto->bdheadtype = sto->bd_vattr[Mhead[0] - 1];
    
    sto->tailmaxl = IN_DEG[Mtail[0]] + OUT_DEG[Mtail[0]] == sto->bound;
    sto->headmaxl = IN_DEG[Mhead[0]] + OUT_DEG[Mhead[0]] == sto->bound;
  } else {
    // strat and bd types already set above
    sto->tailmaxl = IN_DEG[Mtail[0]] + OUT_DEG[Mtail[0]] == sto->bound - 1;
    sto->headmaxl = IN_DEG[Mhead[0]] + OUT_DEG[Mhead[0]] == sto->bound - 1;
  }
    
  // here we compute the proposedcumprob, checking only those
  // mixing types that can be influenced by toggles made on 
  // the current mixing type
  sto->proposedcumprob = sto->currentcumprob;
  // avoid these somewhat expensive checks in the typical case
  // where you have enough submaximal nodes that you cannot
  // be exhausting any mixing types of toggleable dyads
  if(sto->attrcounts[sto->strattailtype][sto->bdtailtype] <= 2 || sto->attrcounts[sto->stratheadtype][sto->bdheadtype] <= 2) {
    if(sto->proposedcumprob != 1 && edgeflag) {
      // we are proposing removing an edge of the current mixing type and need to make sure 
      // that any influenced mixing type that couldn't be toggled before this toggle
      // but could be toggled after this toggle gets its prob added to proposedcumprob;
      // note that if proposedcumprob = 1 in the test above then all mixing types were
      // toggleable before this off-toggle and that will remain the case afterward, so
      // there is nothing to do
      int ntocheck = (2 - ((sto->strattailtype == sto->stratheadtype) || BIPARTITE))*sto->nstratlevels;
      for(int i = 0; i < ntocheck; i++) {
        int infl_i;
        if(i < sto->nstratlevels) {
          infl_i = sto->indmat[sto->strattailtype][i];
        } else {
          infl_i = sto->indmat[i - sto->nstratlevels][sto->stratheadtype];  
        }
        if(infl_i < 0 || infl_i == sto->stratmixingtype) {
          continue;
        }

        if(sto->currentprobvec[infl_i] > 0) {
          continue;
        }
        // else there are no toggleable dyads of type infl_i in the current network;
        // we need to check if that changes after hypothetically removing the proposed edge
        int anytoggleable = FALSE;
        
        for(int j = 0; j < (int)sto->BDtypesbyStrattype[infl_i]; j++) {
          // adjustments
          int proposedtailadjustment = (sto->strattailtype == sto->strattailtypes[infl_i] && sto->bdtailtype == sto->BDtailsbyStrattype[infl_i][j] && sto->tailmaxl) + (sto->stratheadtype == sto->strattailtypes[infl_i] && sto->bdheadtype == sto->BDtailsbyStrattype[infl_i][j] && sto->headmaxl);
          int proposedheadadjustment = (sto->strattailtype == sto->stratheadtypes[infl_i] && sto->bdtailtype == sto->BDheadsbyStrattype[infl_i][j] && sto->tailmaxl) + (sto->stratheadtype == sto->stratheadtypes[infl_i] && sto->bdheadtype == sto->BDheadsbyStrattype[infl_i][j] && sto->headmaxl);
          
          int tailcounts = sto->attrcounts[(int)sto->strattailtypes[infl_i]][(int)sto->BDtailsbyStrattype[infl_i][j]];
          int headcounts = sto->attrcounts[(int)sto->stratheadtypes[infl_i]][(int)sto->BDheadsbyStrattype[infl_i][j]];
          
          proposedtailadjustment = -proposedtailadjustment;
          proposedheadadjustment = -proposedheadadjustment;
          
          if(tailcounts > proposedtailadjustment && headcounts > proposedheadadjustment + (sto->strattailtypes[infl_i] == sto->stratheadtypes[infl_i] && sto->BDtailsbyStrattype[infl_i][j] == sto->BDheadsbyStrattype[infl_i][j])) {
            anytoggleable = TRUE;
            break;
          }      
        }
        
        if(anytoggleable) {
          sto->proposedcumprob += sto->originalprobvec[infl_i];
        }
      }
    } else if(!edgeflag) {
      // we are proposing toggling on an edge of the current mixing type, and need to make
      // sure that any influenced mixing type that could be toggled before this toggle
      // can also be toggled after this toggle, or else has its prob subtracted accordingly
      int ntocheck = (2 - ((sto->strattailtype == sto->stratheadtype) || BIPARTITE))*sto->nstratlevels;
      for(int i = 0; i < ntocheck; i++) {
        int infl_i;
        if(i < sto->nstratlevels) {
          infl_i = sto->indmat[sto->strattailtype][i];
        } else {
          infl_i = sto->indmat[i - sto->nstratlevels][sto->stratheadtype];  
        }
        if(infl_i < 0 || infl_i == sto->stratmixingtype) {
          continue;
        }

        if(sto->currentprobvec[infl_i] == 0 || sto->els[infl_i]->nedges > 0) {
          continue;
        }
        // else there are no toggleable dyads of type infl_i in the current network;
        // we need to check if that changes after hypothetically removing the proposed edge
        int anytoggleable = FALSE;
        
        for(int j = 0; j < (int)sto->BDtypesbyStrattype[infl_i]; j++) {
          // adjustments
          int proposedtailadjustment = (sto->strattailtype == sto->strattailtypes[infl_i] && sto->bdtailtype == sto->BDtailsbyStrattype[infl_i][j] && sto->tailmaxl) + (sto->stratheadtype == sto->strattailtypes[infl_i] && sto->bdheadtype == sto->BDtailsbyStrattype[infl_i][j] && sto->headmaxl);
          int proposedheadadjustment = (sto->strattailtype == sto->stratheadtypes[infl_i] && sto->bdtailtype == sto->BDheadsbyStrattype[infl_i][j] && sto->tailmaxl) + (sto->stratheadtype == sto->stratheadtypes[infl_i] && sto->bdheadtype == sto->BDheadsbyStrattype[infl_i][j] && sto->headmaxl);
          
          int tailcounts = sto->attrcounts[(int)sto->strattailtypes[infl_i]][(int)sto->BDtailsbyStrattype[infl_i][j]];
          int headcounts = sto->attrcounts[(int)sto->stratheadtypes[infl_i]][(int)sto->BDheadsbyStrattype[infl_i][j]];
                
          if(tailcounts > proposedtailadjustment && headcounts > proposedheadadjustment + (sto->strattailtypes[infl_i] == sto->stratheadtypes[infl_i] && sto->BDtailsbyStrattype[infl_i][j] == sto->BDheadsbyStrattype[infl_i][j])) {
            anytoggleable = TRUE;
            break;
          }      
        }
        
        if(!anytoggleable) {
          sto->proposedcumprob -= sto->originalprobvec[infl_i];
        }
      }    
    }
  }
  
  // need to compute proposed dyad count for current mixing type (only)
  Dyad proposeddyadstype = ndyadstype;

  int delta = edgeflag ? +1 : -1;

  for(int j = 0; j < (int)sto->BDtypesbyStrattype[strat_i]; j++) {
    int corr = 0;
    int ha = 0;

    if(sto->strattailtype == sto->stratheadtypes[strat_i] && sto->bdtailtype == sto->BDheadsbyStrattype[strat_i][j] && sto->tailmaxl) {
      ha += delta;
      corr += sto->attrcounts[(int)sto->strattailtypes[strat_i]][(int)sto->BDtailsbyStrattype[strat_i][j]];
    }
      
    if(sto->stratheadtype == sto->stratheadtypes[strat_i] && sto->bdheadtype == sto->BDheadsbyStrattype[strat_i][j] && sto->headmaxl) {
      ha += delta;
      corr += sto->attrcounts[(int)sto->strattailtypes[strat_i]][(int)sto->BDtailsbyStrattype[strat_i][j]];        
    }
      
    if(sto->strattailtype == sto->strattailtypes[strat_i] && sto->bdtailtype == sto->BDtailsbyStrattype[strat_i][j] && sto->tailmaxl) {
      corr += sto->attrcounts[(int)sto->stratheadtypes[strat_i]][(int)sto->BDheadsbyStrattype[strat_i][j]] + ha;
    }
      
    if(sto->stratheadtype == sto->strattailtypes[strat_i] && sto->bdheadtype == sto->BDtailsbyStrattype[strat_i][j] && sto->headmaxl) {
      corr += sto->attrcounts[(int)sto->stratheadtypes[strat_i]][(int)sto->BDheadsbyStrattype[strat_i][j]] + ha;
    }
      
    if(sto->strattailtypes[strat_i] == sto->stratheadtypes[strat_i] && sto->BDtailsbyStrattype[strat_i][j] == sto->BDheadsbyStrattype[strat_i][j]) {
      if(edgeflag) {
        corr -= ha;
      } else {
        corr += ha;
      }
      corr /= 2;
    }
    
    if(edgeflag) {
      proposeddyadstype += corr;
    } else {
      proposeddyadstype -= corr;
    }      
  }
  
  int proposedsubmaxledgestype = sto->currentsubmaxledgestype[strat_i];
  
  // if we are adding an edge that will be submaximal in the post-toggle 
  // network, then increment proposedsubmaxledgestype for this particular edge
  if(!edgeflag && !sto->tailmaxl && !sto->headmaxl) {
    proposedsubmaxledgestype++;
  }
  
  // if we are removing an edge that is submaximal in the current
  // network, decrement proposedsubmaxledgestype for this particular edge
  if(edgeflag && !sto->tailmaxl && !sto->headmaxl) {
    proposedsubmaxledgestype--;
  }

  Edge e;
  Vertex v;

  // if tail will change maximality on toggle, then adjust
  // proposedsubmaxledgestype for all edges between tail and
  // a submaximal neighbor v with the edge between tail and v
  // having the mixing type strat_i, taking care not to count head,
  // since that was handled separately above
  if(sto->tailmaxl) {
    STEP_THROUGH_OUTEDGES(Mtail[0], e, v) {
      if(v != Mhead[0] && IN_DEG[v] + OUT_DEG[v] < sto->bound && sto->indmat[sto->strattailtype][(int)sto->strat_vattr[v - 1]] == strat_i) {
        proposedsubmaxledgestype += delta;
      }
    }
    STEP_THROUGH_INEDGES(Mtail[0], e, v) {
      if(IN_DEG[v] + OUT_DEG[v] < sto->bound && sto->indmat[sto->strattailtype][(int)sto->strat_vattr[v - 1]] == strat_i) {
        proposedsubmaxledgestype += delta;
      }
    }
  }
  
  // ditto head
  if(sto->headmaxl) {
    STEP_THROUGH_OUTEDGES(Mhead[0], e, v) {
      if(IN_DEG[v] + OUT_DEG[v] < sto->bound && sto->indmat[sto->stratheadtype][(int)sto->strat_vattr[v - 1]] == strat_i) {
        proposedsubmaxledgestype += delta;
      }
    }
    STEP_THROUGH_INEDGES(Mhead[0], e, v) {
      if(v != Mtail[0] && IN_DEG[v] + OUT_DEG[v] < sto->bound && sto->indmat[sto->stratheadtype][(int)sto->strat_vattr[v - 1]] == strat_i) {
        proposedsubmaxledgestype += delta;
      }
    }
  }
  
  double prob_weight = sto->currentcumprob/sto->proposedcumprob;
  
  // the rationale for the logratio is similar to that given for BDTNT, with two additional considerations:
  // 
  // - counts of edges and BD toggleable dyads should be for the current mixing type only, and
  //
  // - it is possible for a strat mixing type to reach zero toggleable dyads (by having no edges and also no BD toggleable dyads);
  //   such a strat mixing type cannot be selected when we choose the strat mixing type at the top of the P_FN, and so we must 
  //   "disable" it until it comes to have toggleable dyads again; the term prob_weight adjusts for the fact that the total weight 
  //   given to toggleable strat mixing types may be different in the current and proposed networks, and thus the probability
  //   to select the current strat mixing type may be different in the current and proposed networks
  
  if(edgeflag) {
    MHp->logratio = log(prob_weight*(((nedgestype == 1 ? 1.0 : 0.5)/proposeddyadstype))/(((ndyadstype == 0 ? 1.0/nedgestype : (0.5/nedgestype)*(1 + (double)sto->currentsubmaxledgestype[strat_i]/ndyadstype)))));
  } else {
    MHp->logratio = log(prob_weight*((proposeddyadstype == 0 ? 1.0/(nedgestype + 1) : (0.5/(nedgestype + 1))*(1 + (double)proposedsubmaxledgestype/proposeddyadstype))/((nedgestype == 0 ? 1.0 : 0.5)/ndyadstype)));
  }
}

MH_U_FN(Mu_BDStratTNT) {
  GET_STORAGE(BDStratTNTStorage, sto);

  // skip the next block if we have enough submaximal nodes that
  // no strat type toggleability status can possibly have changed
  if(sto->attrcounts[sto->strattailtype][sto->bdtailtype] <= 2 || sto->attrcounts[sto->stratheadtype][sto->bdheadtype] <= 2) {
    // avoid copying in the common case where all strat types are toggleable in both the current and proposed networks
    if(sto->currentcumprob != 1 || sto->proposedcumprob != 1) {
      // copy cumprobs in case they're different
      sto->currentcumprob = sto->proposedcumprob;
    
      if(edgeflag) {
        int ntocheck = (2 - ((sto->strattailtype == sto->stratheadtype) || BIPARTITE))*sto->nstratlevels;
        for(int i = 0; i < ntocheck; i++) {
          int infl_i;
          if(i < sto->nstratlevels) {
            infl_i = sto->indmat[sto->strattailtype][i];
          } else {
            infl_i = sto->indmat[i - sto->nstratlevels][sto->stratheadtype];  
          }
          if(infl_i < 0 || infl_i == sto->stratmixingtype) {
            continue;
          }

          if(sto->currentprobvec[infl_i] > 0) {
            continue;
          }
  
          int anytoggleable = FALSE;
          
          for(int j = 0; j < (int)sto->BDtypesbyStrattype[infl_i]; j++) {
            // adjustments
            int proposedtailadjustment = (sto->strattailtype == sto->strattailtypes[infl_i] && sto->bdtailtype == sto->BDtailsbyStrattype[infl_i][j] && sto->tailmaxl) + (sto->stratheadtype == sto->strattailtypes[infl_i] && sto->bdheadtype == sto->BDtailsbyStrattype[infl_i][j] && sto->headmaxl);
            int proposedheadadjustment = (sto->strattailtype == sto->stratheadtypes[infl_i] && sto->bdtailtype == sto->BDheadsbyStrattype[infl_i][j] && sto->tailmaxl) + (sto->stratheadtype == sto->stratheadtypes[infl_i] && sto->bdheadtype == sto->BDheadsbyStrattype[infl_i][j] && sto->headmaxl);
            
            int tailcounts = sto->attrcounts[(int)sto->strattailtypes[infl_i]][(int)sto->BDtailsbyStrattype[infl_i][j]];
            int headcounts = sto->attrcounts[(int)sto->stratheadtypes[infl_i]][(int)sto->BDheadsbyStrattype[infl_i][j]];
            
            proposedtailadjustment = -proposedtailadjustment;
            proposedheadadjustment = -proposedheadadjustment;
            
            if(tailcounts > proposedtailadjustment && headcounts > proposedheadadjustment + (sto->strattailtypes[infl_i] == sto->stratheadtypes[infl_i] && sto->BDtailsbyStrattype[infl_i][j] == sto->BDheadsbyStrattype[infl_i][j])) {
              anytoggleable = TRUE;
              break;
            }      
          }
          
          if(anytoggleable) {
            sto->currentprobvec[infl_i] = sto->originalprobvec[infl_i];
            WtPopSetWt(infl_i, sto->originalprobvec[infl_i], sto->wtp);
          }
        }
      } else {
        int ntocheck = (2 - ((sto->strattailtype == sto->stratheadtype) || BIPARTITE))*sto->nstratlevels;
        for(int i = 0; i < ntocheck; i++) {
          int infl_i;
          if(i < sto->nstratlevels) {
            infl_i = sto->indmat[sto->strattailtype][i];
          } else {
            infl_i = sto->indmat[i - sto->nstratlevels][sto->stratheadtype];  
          }
          if(infl_i < 0 || infl_i == sto->stratmixingtype) {
            continue;
          }

          if(sto->currentprobvec[infl_i] == 0 || sto->els[infl_i]->nedges > 0) {
            continue;
          }
  
          int anytoggleable = FALSE;
          
          for(int j = 0; j < (int)sto->BDtypesbyStrattype[infl_i]; j++) {
            // adjustments
            int proposedtailadjustment = (sto->strattailtype == sto->strattailtypes[infl_i] && sto->bdtailtype == sto->BDtailsbyStrattype[infl_i][j] && sto->tailmaxl) + (sto->stratheadtype == sto->strattailtypes[infl_i] && sto->bdheadtype == sto->BDtailsbyStrattype[infl_i][j] && sto->headmaxl);
            int proposedheadadjustment = (sto->strattailtype == sto->stratheadtypes[infl_i] && sto->bdtailtype == sto->BDheadsbyStrattype[infl_i][j] && sto->tailmaxl) + (sto->stratheadtype == sto->stratheadtypes[infl_i] && sto->bdheadtype == sto->BDheadsbyStrattype[infl_i][j] && sto->headmaxl);
            
            int tailcounts = sto->attrcounts[(int)sto->strattailtypes[infl_i]][(int)sto->BDtailsbyStrattype[infl_i][j]];
            int headcounts = sto->attrcounts[(int)sto->stratheadtypes[infl_i]][(int)sto->BDheadsbyStrattype[infl_i][j]];
                      
            if(tailcounts > proposedtailadjustment && headcounts > proposedheadadjustment + (sto->strattailtypes[infl_i] == sto->stratheadtypes[infl_i] && sto->BDtailsbyStrattype[infl_i][j] == sto->BDheadsbyStrattype[infl_i][j])) {
              anytoggleable = TRUE;
              break;
            }      
          }
          
          if(!anytoggleable) {
            sto->currentprobvec[infl_i] = 0;
            WtPopSetWt(infl_i, 0, sto->wtp);
          }
        }    
      }
    }
  }
  
  if(edgeflag) {
    // we are removing an existing edge
    UnsrtELDelete(tail, head, sto->els[sto->stratmixingtype]);
    
    if(sto->tailmaxl) {
      // tail will be newly submaxl after toggle, so add it to the appropriate node list
      sto->nodesvec[sto->strattailtype][sto->bdtailtype][sto->attrcounts[sto->strattailtype][sto->bdtailtype]] = tail;
      sto->attrcounts[sto->strattailtype][sto->bdtailtype]++;
    }
    
    if(sto->headmaxl) {
      // head will be newly submaxl after toggle, so add it to the appropriate node list    
      sto->nodesvec[sto->stratheadtype][sto->bdheadtype][sto->attrcounts[sto->stratheadtype][sto->bdheadtype]] = head;
      sto->attrcounts[sto->stratheadtype][sto->bdheadtype]++;
    }
  } else {
    // we are adding a new edge
    UnsrtELInsert(tail, head, sto->els[sto->stratmixingtype]);
        
    if(sto->tailmaxl) {
      // tail will be newly maxl after toggle, so remove it from the appropriate node list
      sto->nodesvec[sto->strattailtype][sto->bdtailtype][sto->tailindex] = sto->nodesvec[sto->strattailtype][sto->bdtailtype][sto->attrcounts[sto->strattailtype][sto->bdtailtype] - 1];
      sto->attrcounts[sto->strattailtype][sto->bdtailtype]--;
      
      // if we just moved the head, update its index
      if((sto->strattailtype == sto->stratheadtype) && (sto->bdtailtype == sto->bdheadtype) && (sto->headindex == sto->attrcounts[sto->strattailtype][sto->bdtailtype])) {
        sto->headindex = sto->tailindex;
      }
    }
    
    if(sto->headmaxl) {
      // head will be newly maxl after toggle, so remove it from the appropriate node list
      sto->nodesvec[sto->stratheadtype][sto->bdheadtype][sto->headindex] = sto->nodesvec[sto->stratheadtype][sto->bdheadtype][sto->attrcounts[sto->stratheadtype][sto->bdheadtype] - 1];
      sto->attrcounts[sto->stratheadtype][sto->bdheadtype]--;
    }       
  }
  
  // if we are adding an edge that will be submaximal in the post-toggle 
  // network, then increment currentsubmaxledgestype for this particular edge
  if(!edgeflag && !sto->tailmaxl && !sto->headmaxl) {
    sto->currentsubmaxledgestype[sto->stratmixingtype]++;
  }
  
  // if we are removing an edge that is submaximal in the current
  // network, decrement currentsubmaxledgestype for this particular edge
  if(edgeflag && !sto->tailmaxl && !sto->headmaxl) {
    sto->currentsubmaxledgestype[sto->stratmixingtype]--;
  }

  int delta = edgeflag ? +1 : -1;

  Edge e;
  Vertex v;

  // if tail will change maximality on toggle, then adjust
  // currentsubmaxledgestype for all edges between tail and
  // a submaximal neighbor v, taking care not to count head,
  // since that was handled separately above
  if(sto->tailmaxl) {
    STEP_THROUGH_OUTEDGES(tail, e, v) {
      if(v != head && IN_DEG[v] + OUT_DEG[v] < sto->bound && sto->indmat[sto->strattailtype][(int)sto->strat_vattr[v - 1]] >= 0) {
        sto->currentsubmaxledgestype[(int)sto->indmat[sto->strattailtype][(int)sto->strat_vattr[v - 1]]] += delta;
      }
    }
    STEP_THROUGH_INEDGES(tail, e, v) {
      if(IN_DEG[v] + OUT_DEG[v] < sto->bound && sto->indmat[sto->strattailtype][(int)sto->strat_vattr[v - 1]] >= 0) {
        sto->currentsubmaxledgestype[(int)sto->indmat[sto->strattailtype][(int)sto->strat_vattr[v - 1]]] += delta;
      }
    }
  }
  
  // ditto head
  if(sto->headmaxl) {
    STEP_THROUGH_OUTEDGES(head, e, v) {
      if(IN_DEG[v] + OUT_DEG[v] < sto->bound && sto->indmat[sto->stratheadtype][(int)sto->strat_vattr[v - 1]] >= 0) {
        sto->currentsubmaxledgestype[(int)sto->indmat[sto->stratheadtype][(int)sto->strat_vattr[v - 1]]] += delta;
      }
    }
    STEP_THROUGH_INEDGES(head, e, v) {
      if(v != tail && IN_DEG[v] + OUT_DEG[v] < sto->bound && sto->indmat[sto->stratheadtype][(int)sto->strat_vattr[v - 1]] >= 0) {
        sto->currentsubmaxledgestype[(int)sto->indmat[sto->stratheadtype][(int)sto->strat_vattr[v - 1]]] += delta;
      }
    }
  }
}

MH_F_FN(Mf_BDStratTNT) {
  // Free all the things
  GET_STORAGE(BDStratTNTStorage, sto);

  int nattrcodes = asInteger(getListElement(MHp->R, "nattrcodes"));
  int bdlevels = asInteger(getListElement(MHp->R, "bd_levels"));

  for(int i = 0; i < nattrcodes; i++) {
    Free(sto->attrcounts[i]);
  }
  Free(sto->attrcounts);

  for(int i = 0; i < nattrcodes; i++) {
    for(int j = 0; j < bdlevels; j++) {
      Free(sto->nodesvec[i][j]);
    }
    Free(sto->nodesvec[i]);
  }
  Free(sto->nodesvec);
  
  for(int i = 0; i < sto->nmixtypes; i++) {
    UnsrtELDestroy(sto->els[i]);
  }
  Free(sto->els);

  Free(sto->currentsubmaxledgestype);
  Free(sto->indmat);

  Free(sto->BDtailsbyStrattype);
  Free(sto->BDheadsbyStrattype);

  Free(sto->originalprobvec);
  Free(sto->currentprobvec);
  
  WtPopDestroy(sto->wtp);
  // MHp->storage itself should be Freed by MHProposalDestroy
}


/********************
    MH_BDTNT

This proposal handles a constant outdegree upper bound and uses a
whitelist of blocks (sets of dyads defined by combinations of vertex
attributes) so that specific combinations can be forbidden.
********************/

typedef struct {
  int *attrcounts; // Count of the number of nodes with each attribute type i that are "submaximal degree" (attrcounts[i] lengths of nodesvec[i]).
  Vertex **nodesvec; // List of lists of submaximal nodes of attribute i.
  
  UnsrtEL *edgelist; // All edges in the network.
  
  int tailtype; // Attribute type of the last tail to be proposed.
  int tailindex; // Index of that tail in nodesvec[i] where i is tail type.
  int tailmaxl; // Will the tail change the maximality status if the current proposal is accepted?
  
  int headtype; // Ditto for heads.
  int headindex;
  int headmaxl;
  
  Dyad currentdyads; // Number of dyads that can be selected in the current network.
  Dyad proposeddyads; // As above, but if the proposal is accepted.
  
  int currentsubmaxledges;  // Number of edges in the current network both of whose endpoints are submaximal
  int proposedsubmaxledges; // Number of edges in the proposed network both of whose endpoints are submaximal
  
  int bound; // Single upper bound on degree.
  int nmixtypes; // Number of pairings of attributes.
  double *vattr; // Vertex attributes.
  // Parallel vectors of attribute combinations that are allowed.
  double *tailtypes;
  double *headtypes;
} BDTNTStorage;

MH_I_FN(Mi_BDTNT) {
  // process the inputs and initialize all the node lists in storage; set MHp->ntoggles to 1
  MHp->ntoggles = 1;
  
  int bound = MHp->inputs[0]; // As in struct.
  int nlevels = MHp->inputs[1]; // Number of distinct types of types of vertex.
  
  double *nodecountsbycode = MHp->inputs + 2; // Number of nodes of each type.
  
  int nmixtypes = MHp->inputs[2 + nlevels]; // As in struct.
  
  double *tailtypes = MHp->inputs + 3 + nlevels; // As in struct.
  double *headtypes = tailtypes + nmixtypes; // As in struct.
  
  double *vattr = headtypes + nmixtypes; // As in struct.
    
  Vertex **nodesvec = (Vertex **)Calloc(nlevels, Vertex *); // As in struct.
  
  int *attrcounts = (int *)Calloc(nlevels, int); // As in struct.
    
  for(int i = 0; i < nlevels; i++) {
    // make room for maximum number of nodes of each type
    nodesvec[i] = (Vertex *)Calloc((int)nodecountsbycode[i], Vertex);
  }

  // Populate the list of submaximal vertices by checking whether a given vertex has a maximal degree.
  for(Vertex vertex = 1; vertex <= N_NODES; vertex++) {
    if(IN_DEG[vertex] + OUT_DEG[vertex] < bound) {
      // add vertex to the submaximal list corresponding to its attribute type
      nodesvec[(int)vattr[vertex - 1]][attrcounts[(int)vattr[vertex - 1]]] = vertex;
      attrcounts[(int)vattr[vertex - 1]]++;
    }
  }
  
  // count number of "BD-toggleable" dyads in current network
  Dyad currentdyads = 0;    
  for(int i = 0; i < nmixtypes; i++) {
    if(tailtypes[i] == headtypes[i]) { // Includes a diagonal dyad.
      currentdyads += (Dyad)attrcounts[(int)tailtypes[i]]*(attrcounts[(int)headtypes[i]] - 1)/2;
    } else { // Includes a nondiagonal dyad.
      currentdyads += (Dyad)attrcounts[(int)tailtypes[i]]*attrcounts[(int)headtypes[i]];
    }
  }

  // if we cannot toggle any edges or dyads, error
  if(EDGECOUNT(nwp) == 0 && currentdyads == 0) {
    MHp->ntoggles = MH_FAILED;
    return;
  }  
  
  ALLOC_STORAGE(1, BDTNTStorage, sto);
  
  sto->attrcounts = attrcounts;
  sto->nodesvec = nodesvec;
  sto->currentdyads = currentdyads;
  sto->bound = bound;
  sto->nmixtypes = nmixtypes;
  sto->vattr = vattr;
  sto->tailtypes = tailtypes;
  sto->headtypes = headtypes;

  // Construct and populate the list of edges. (May be obviated by more efficient network sampling.)
  sto->edgelist = UnsrtELInitialize(0, NULL, NULL, FALSE);
  Vertex head;
  Edge e;
  for(Vertex tail = 1; tail <= N_NODES; tail++) {
    STEP_THROUGH_OUTEDGES(tail, e, head) {
      UnsrtELInsert(tail, head, sto->edgelist);
      if(IN_DEG[tail] + OUT_DEG[tail] < bound && IN_DEG[head] + OUT_DEG[head] < bound) {
        sto->currentsubmaxledges++;
      }
    }
  }
}

MH_P_FN(MH_BDTNT) {    
  GET_STORAGE(BDTNTStorage, sto);

  int nedges = EDGECOUNT(nwp);
  
  int edgeflag;

  // if currentdyads == 0, we *must* propose toggling off an existing edge;
  // the case nedges == 0 && currentdyads == 0 was excluded during initialization,
  // and we cannot end up in that case if we don't start in that case
  // (assuming the initial network is valid)  
  if((unif_rand() < 0.5 && nedges > 0) || (sto->currentdyads == 0)) {
    // select an existing edge at random, and propose toggling it off
    UnsrtELGetRand(Mtail, Mhead, sto->edgelist);
        
    edgeflag = TRUE;
  } else {
    // select a BD-toggleable dyad and propose toggling it
    // doubling here allows more efficient calculation in 
    // the case tailtypes[i] == headtypes[i]
    
    Dyad dyadindex = 2*sto->currentdyads*unif_rand();
            
    Vertex head;
    Vertex tail;
    
    // This rather ugly block of code is just finding the dyad that corresponds
    // to the dyadindex we drew above, and then setting the info for
    // tail and head appropriately.
    //
    // Note that the dyad block selector cannot select an edge
    // incident on a maximal node; but the edge "reselection" below
    // will be able to select it with equal probability to the others,
    // equalising its chances of being selected, and requiring only a
    // "marginal" adjustment to the acceptance probability.
    for(int i = 0; i < sto->nmixtypes; i++) {
      Dyad dyadstype; // Number of dyads in that block.
      if(sto->tailtypes[i] == sto->headtypes[i]) {
        dyadstype = (Dyad)sto->attrcounts[(int)sto->tailtypes[i]]*(sto->attrcounts[(int)sto->headtypes[i]] - 1)/2;
      } else {
        dyadstype = (Dyad)sto->attrcounts[(int)sto->tailtypes[i]]*sto->attrcounts[(int)sto->headtypes[i]];
      }
      
      if(dyadindex < 2*dyadstype) { // We are in the right block.
        int tailindex;
        int headindex;
        
        if(sto->tailtypes[i] == sto->headtypes[i]) {
          tailindex = dyadindex / sto->attrcounts[(int)sto->headtypes[i]];
          headindex = dyadindex % (sto->attrcounts[(int)sto->headtypes[i]] - 1);
          if(tailindex == headindex) {
            headindex = sto->attrcounts[(int)sto->headtypes[i]] - 1;
          }
                    
          tail = sto->nodesvec[(int)sto->tailtypes[i]][tailindex];
          head = sto->nodesvec[(int)sto->headtypes[i]][headindex];
          
        } else {
          dyadindex /= 2;
          tailindex = dyadindex / sto->attrcounts[(int)sto->headtypes[i]];
          headindex = dyadindex % sto->attrcounts[(int)sto->headtypes[i]];
          
          tail = sto->nodesvec[(int)sto->tailtypes[i]][tailindex];
          head = sto->nodesvec[(int)sto->headtypes[i]][headindex];
        }
        
        if(tail > head) {
          sto->tailindex = headindex;
          sto->headindex = tailindex;
          Mtail[0] = head;
          Mhead[0] = tail;
        } else {
          sto->tailindex = tailindex;
          sto->headindex = headindex;
          Mtail[0] = tail;
          Mhead[0] = head;
        }
        
        break;
      } else { // Decrement index and continue.
        dyadindex -= 2*dyadstype;
      }
    }

    // Resample the edge to make it known to the unsorted edgelist.
    edgeflag = IS_OUTEDGE(Mtail[0],Mhead[0]);
    if(edgeflag) UnsrtELGetRand(Mtail, Mhead, sto->edgelist);
  }
  
  sto->tailtype = sto->vattr[Mtail[0] - 1];
  sto->headtype = sto->vattr[Mhead[0] - 1];    
  
  if(edgeflag) {
    sto->tailmaxl = IN_DEG[Mtail[0]] + OUT_DEG[Mtail[0]] == sto->bound;
    sto->headmaxl = IN_DEG[Mhead[0]] + OUT_DEG[Mhead[0]] == sto->bound;   
  } else {
    sto->tailmaxl = IN_DEG[Mtail[0]] + OUT_DEG[Mtail[0]] == sto->bound - 1;
    sto->headmaxl = IN_DEG[Mhead[0]] + OUT_DEG[Mhead[0]] == sto->bound - 1;
  }
        
  // the count of dyads that can be toggled in the "GetRandBDDyad" branch,
  // in the proposed network
  sto->proposeddyads = sto->currentdyads;

  // Update the number of toggleable dyads.
  int delta = edgeflag ? +1 : -1;
  
  for(int i = 0; i < sto->nmixtypes; i++) {
    int corr = 0; // Change to the number of dyads that can be selected.
    int ha = 0; // "Head adjustment": prevent double-counting.

    if(sto->tailtype == sto->headtypes[i] && sto->tailmaxl) {
      ha += delta;
      corr += sto->attrcounts[(int)sto->tailtypes[i]];
    }
      
    if(sto->headtype == sto->headtypes[i] && sto->headmaxl) {
      ha += delta;
      corr += sto->attrcounts[(int)sto->tailtypes[i]];        
    }
      
    if(sto->tailtype == sto->tailtypes[i] && sto->tailmaxl) {
      corr += sto->attrcounts[(int)sto->headtypes[i]] + ha;
    }
      
    if(sto->headtype == sto->tailtypes[i] && sto->headmaxl) {
      corr += sto->attrcounts[(int)sto->headtypes[i]] + ha;
    }
      
    if(sto->tailtypes[i] == sto->headtypes[i]) {
      if(edgeflag) {
        corr -= ha;
      } else {
        corr += ha;
      }
      corr /= 2;
    }
    
    if(edgeflag) {
      sto->proposeddyads += corr;
    } else {
      sto->proposeddyads -= corr;
    }
  }
  
  sto->proposedsubmaxledges = sto->currentsubmaxledges;
  
  // if we are adding an edge that will be submaximal in the post-toggle 
  // network, then increment proposedsubmaxledges for this particular edge
  if(!edgeflag && !sto->tailmaxl && !sto->headmaxl) {
    sto->proposedsubmaxledges++;
  }
  
  // if we are removing an edge that is submaximal in the current
  // network, decrement proposedsubmaxledges for this particular edge
  if(edgeflag && !sto->tailmaxl && !sto->headmaxl) {
    sto->proposedsubmaxledges--;
  }

  Edge e;
  Vertex v;

  // if tail will change maximality on toggle, then adjust
  // proposedsubmaxledges for all edges between tail and
  // a submaximal neighbor v, taking care not to count head,
  // since that was handled separately above
  if(sto->tailmaxl) {
    STEP_THROUGH_OUTEDGES(Mtail[0], e, v) {
      if(v != Mhead[0] && IN_DEG[v] + OUT_DEG[v] < sto->bound) {
        sto->proposedsubmaxledges += delta;
      }
    }
    STEP_THROUGH_INEDGES(Mtail[0], e, v) {
      if(IN_DEG[v] + OUT_DEG[v] < sto->bound) {
        sto->proposedsubmaxledges += delta;
      }
    }
  }
  
  // ditto head
  if(sto->headmaxl) {
    STEP_THROUGH_OUTEDGES(Mhead[0], e, v) {
      if(IN_DEG[v] + OUT_DEG[v] < sto->bound) {
        sto->proposedsubmaxledges += delta;
      }
    }
    STEP_THROUGH_INEDGES(Mhead[0], e, v) {
      if(v != Mtail[0] && IN_DEG[v] + OUT_DEG[v] < sto->bound) {
        sto->proposedsubmaxledges += delta;
      }
    }
  }
  
  // rationale for the logratio:
  //
  // there is only one way to select a non-edge, and that is via the initial dyad sample in the "GetRandBDDyad" branch above;
  // all "BD toggleable dyads" are given equal weight in the initial dyad sampling in the GetRandBDDyad branch,
  // so if one has entered the GetRandBDDyad branch, the probability to select a given non-edge is 1 over the number of 
  // BD toggleable dyads, which is sto->currentdyads in the current network and proposeddyads in the proposed network; 
  // given that the network has at least one BD toggleable dyad, the probability to enter the GetRandBDDyad branch 
  // is either 1/2 if the network has any edges or 1 if the network has no edges; this fully explains the calculation 
  // of non-edge sampling probabilities below
  //
  // there are two ways to select an edge: it can be sampled directly in the "GetRandEdge" branch above, or it can be sampled 
  // indirectly by entering the GetRandBDDyad branch, sampling a submaximal edge, and then resampling an arbitrary edge;
  // in the GetRandEdge branch, the probability to select a given edge is 1 over the number of edges in the network;
  // in the GetRandBDDyad branch, the probability to select a submaximal edge in the initial dyad sample is the number of
  // submaximal edges divided by the number of BD toggleable dyads, and given that a submaximal edge was selected as the initial
  // dyad sample, the probability to select a given edge on resampling is 1 over the number of edges; given that an edge exists
  // in the network, the probability to enter to GetRandEdge branch is either 1/2 if any BD toggleable dyads exist in the network,
  // or is 1 if no BD toggleable dyads exist in the network; the probability to enter the GetRandBDDyad branch is 1 minus the
  // probability to enter the GetRandEdge branch; this fully explains the calculation of edge sampling probabilities below
  
  if(edgeflag) {
    MHp->logratio = log(((nedges == 1 ? 1.0 : 0.5)/sto->proposeddyads)/(sto->currentdyads == 0 ? 1.0/nedges : (0.5/nedges)*(1 + ((double)sto->currentsubmaxledges/sto->currentdyads))));
  } else {
    MHp->logratio = log((sto->proposeddyads == 0 ? 1.0/(nedges + 1) : (0.5/(nedges + 1))*(1 + (double)sto->proposedsubmaxledges/sto->proposeddyads))/((nedges == 0 ? 1.0 : 0.5)/sto->currentdyads));  
  }
}

// this U_FN is called *before* the toggle is made in the network
MH_U_FN(Mu_BDTNT) {  
  GET_STORAGE(BDTNTStorage, sto);
  
  if(edgeflag) {
    // we are removing an edge
    UnsrtELDelete(tail, head, sto->edgelist);
    
    if(sto->tailmaxl) {
      // tail will be newly submaxl after toggle, so add it to the appropriate node list
      sto->nodesvec[sto->tailtype][sto->attrcounts[sto->tailtype]] = tail;
      sto->attrcounts[sto->tailtype]++;
    }
    
    if(sto->headmaxl) {
      // head will be newly submaxl after toggle, so add it to the appropriate node list    
      sto->nodesvec[sto->headtype][sto->attrcounts[sto->headtype]] = head;
      sto->attrcounts[sto->headtype]++;
    }
  } else {
    // we are adding an edge
    UnsrtELInsert(tail, head, sto->edgelist);
    
    if(sto->tailmaxl) {
      // tail will be newly maxl after toggle, so remove it from the appropriate node list
      sto->nodesvec[sto->tailtype][sto->tailindex] = sto->nodesvec[sto->tailtype][sto->attrcounts[sto->tailtype] - 1];
      sto->attrcounts[sto->tailtype]--;
      
      // if we just moved the head, update its index
      if((sto->tailtype == sto->headtype) && (sto->headindex == sto->attrcounts[sto->tailtype])) {
        sto->headindex = sto->tailindex;
      }
    }
    
    if(sto->headmaxl) {
      // head will be newly maxl after toggle, so remove it from the appropriate node list
      sto->nodesvec[sto->headtype][sto->headindex] = sto->nodesvec[sto->headtype][sto->attrcounts[sto->headtype] - 1];
      sto->attrcounts[sto->headtype]--;
    }   
  }
  
  // update current dyad count
  sto->currentdyads = sto->proposeddyads;

  // update the current submaximal edge count
  sto->currentsubmaxledges = sto->proposedsubmaxledges;
}

MH_F_FN(Mf_BDTNT) {
  // Free all the things
  GET_STORAGE(BDTNTStorage, sto);
  
  int nlevels = MHp->inputs[1];

  for(int i = 0; i < nlevels; i++) {
    Free(sto->nodesvec[i]);
  }

  Free(sto->nodesvec);
  Free(sto->attrcounts);

  UnsrtELDestroy(sto->edgelist);
  // MHp->storage itself should be Freed by MHProposalDestroy
}

/********************
    MH_StratTNT
********************/

typedef struct {
  UnsrtEL **els;
  int currentmixingtype;
  double **nodesbycode;
  
  int nmixtypes;

  double *pmat;
  WtPop *wtp;
  
  double *tailtypes;
  double *headtypes;

  Dyad *ndyadstype;
  double *nodecountsbycode;
} StratTNTStorage;

MH_I_FN(Mi_StratTNT) {
  ALLOC_STORAGE(1, StratTNTStorage, sto);
    
  // process the inputs and initialize all the edgelists in storage; set MHp->ntoggles to 1
  MHp->ntoggles = 1;
  
  int nmixtypes = MHp->inputs[0];
    
  int nattrcodes = MHp->inputs[1 + 3*nmixtypes];
  
  double *vattr = MHp->inputs + 1 + 3*nmixtypes + 1 + nattrcodes + N_NODES;
    
  UnsrtEL **els = (UnsrtEL **)Calloc(nmixtypes, UnsrtEL *);
      
  for(int i = 0; i < nmixtypes; i++) {
    els[i] = UnsrtELInitialize(0, NULL, NULL, FALSE);
  }
  
  double *inputindmat = MHp->inputs + 1 + 3*nmixtypes + 1 + nattrcodes + N_NODES + N_NODES;  
  
  double **indmat = (double **)Calloc(nattrcodes, double *);
  indmat[0] = inputindmat;
  for(int i = 1; i < nattrcodes; i++) {
    indmat[i] = indmat[i - 1] + nattrcodes;
  }
  
  Vertex head;
  Edge e;
  for(Vertex tail = 1; tail <= N_NODES; tail++) {
    STEP_THROUGH_OUTEDGES(tail, e, head) {
      int index = indmat[(int)vattr[tail - 1]][(int)vattr[head - 1]];
      if(index >= 0) {
        UnsrtELInsert(tail, head, els[index]);
      }
    }
  }
  Free(indmat);
      
  double *nodecountsbycode = MHp->inputs + 1 + 3*nmixtypes + 1;
  
  double **nodesbycode = (double **)Calloc(nattrcodes, double *);
  nodesbycode[0] = MHp->inputs + 1 + 3*nmixtypes + 1 + nattrcodes;
  for(int i = 1; i < nattrcodes; i++) {
    nodesbycode[i] = nodesbycode[i - 1] + (int)nodecountsbycode[i - 1];
  }
  
  sto->pmat = Calloc(nmixtypes, double);
  
  int empirical_flag = MHp->inputs[1 + 3*nmixtypes + 1 + nattrcodes + N_NODES + N_NODES + nattrcodes*nattrcodes];
  if(empirical_flag) {
    sto->pmat[0] = els[0]->nedges;
    for(int i = 1; i < nmixtypes; i++) {
      sto->pmat[i] = sto->pmat[i - 1] + els[i]->nedges;
    }
    
    // empirical_flag with no edges is an error
    if(sto->pmat[nmixtypes - 1] == 0) {
      MHp->ntoggles = MH_FAILED;
      return;
    }
    
    for(int i = 0; i < nmixtypes; i++) {
      sto->pmat[i] /= sto->pmat[nmixtypes - 1];
    }
  } else {
    memcpy(sto->pmat, MHp->inputs + 1 + 2*nmixtypes, nmixtypes*sizeof(double));
  }
  
  sto->els = els;
  sto->nodesbycode = nodesbycode;
  sto->nmixtypes = nmixtypes;

  sto->tailtypes = MHp->inputs + 1;
  sto->headtypes = MHp->inputs + 1 + nmixtypes;
  sto->nodecountsbycode = MHp->inputs + 1 + 3*nmixtypes + 1;

  sto->ndyadstype = Calloc(nmixtypes, Dyad);
  for(int i = 0; i < nmixtypes; i++) {
    int tailtype = sto->tailtypes[i];
    int headtype = sto->headtypes[i];
    
    int tailcounts = sto->nodecountsbycode[tailtype];
    int headcounts = sto->nodecountsbycode[headtype];
    
    if(tailtype == headtype) {
      if(DIRECTED) {
        sto->ndyadstype[i] = (Dyad)tailcounts*(headcounts - 1);
      } else {
        sto->ndyadstype[i] = (Dyad)tailcounts*(headcounts - 1)/2;
      }
    } else {
      sto->ndyadstype[i] = (Dyad)tailcounts*headcounts;
    }
  }
  
  // undo cumsum for new WtPop approach
  for(int i = sto->nmixtypes - 1; i > 0; i--) {
    sto->pmat[i] = sto->pmat[i] - sto->pmat[i - 1];
  }
  
  sto->wtp = WtPopInitialize(sto->nmixtypes, sto->pmat);
}

MH_P_FN(MH_StratTNT) {
  GET_STORAGE(StratTNTStorage, sto);
  
  // sample a strat mixing type on which to make a proposal
  int i = WtPopGetRand(sto->wtp);
  
  // record the mixing type of the toggle, in case it's needed in the U function later
  sto->currentmixingtype = i;    
  
  int tailtype = sto->tailtypes[i];
  int headtype = sto->headtypes[i];
    
  // number of edges of this mixing type
  int nedgestype = sto->els[i]->nedges;

  // number of dyads of this mixing type
  Dyad ndyadstype = sto->ndyadstype[i];
  
  double logratio = 0;

  BD_LOOP({
    if(unif_rand() < 0.5 && nedgestype > 0) {
      // select an existing edge of type i at random, and propose toggling it off
      UnsrtELGetRand(Mtail, Mhead, sto->els[i]);
      
      // logratio is essentially copied from TNT, because the probability of 
      // choosing this particular mixing type cancels upon taking the ratio;
      // still need to count only edges and dyads of the appropriate mixing type, though
  	  logratio = log((nedgestype == 1 ? 1.0/(0.5*ndyadstype + 0.5) :
                      nedgestype / ((double) ndyadstype + nedgestype)));
    } else {
      // select a dyad of type i and propose toggling it        
      int tailindex = sto->nodecountsbycode[tailtype]*unif_rand();
      int headindex;
      if(tailtype == headtype) {
        // need to avoid sampling a loop
        headindex = (sto->nodecountsbycode[headtype] - 1)*unif_rand();
        if(headindex == tailindex) {
          headindex = sto->nodecountsbycode[headtype] - 1;
        }
      } else {
        // any old head will do
        headindex = sto->nodecountsbycode[headtype]*unif_rand();
      }
      
      Vertex tail = sto->nodesbycode[tailtype][tailindex];
      Vertex head = sto->nodesbycode[headtype][headindex];
      
      if(tail > head && !DIRECTED) {
        Vertex tmp = tail;
        tail = head;
        head = tmp;
      }
      
      if(IS_OUTEDGE(tail,head)) {
        // pick a new edge from the edgelist uniformly at random so we know its index
        // and hence don't have to look up the index of the edge tail -> head; this gives
        // the same probability of picking each existing edge as if we used the tail -> head
        // edge, but also allows us to keep the edgelists unsorted (at the cost of generating
        // an extra random index in this case)
        UnsrtELGetRand(Mtail, Mhead, sto->els[i]);

        logratio = log((nedgestype == 1 ? 1.0/(0.5*ndyadstype + 0.5) :
                        nedgestype / ((double) ndyadstype + nedgestype)));
      }else{
        Mtail[0] = tail;
        Mhead[0] = head;
                        
        logratio = log((nedgestype == 0 ? 0.5*ndyadstype + 0.5 :
                        1.0 + (ndyadstype)/((double) nedgestype + 1)));
      }
    }
  });
  
  MHp->logratio += logratio;
}

MH_U_FN(Mu_StratTNT) {
  // add or remove edge from appropriate edgelist
  GET_STORAGE(StratTNTStorage, sto);
  
  if(edgeflag) {
    // we are removing an existing edge
    UnsrtELDelete(tail, head, sto->els[sto->currentmixingtype]);
  } else {
    // we are adding a new edge
    UnsrtELInsert(tail, head, sto->els[sto->currentmixingtype]);
  }
}

MH_F_FN(Mf_StratTNT) {
  // Free all the things
  GET_STORAGE(StratTNTStorage, sto);
  
  for(int i = 0; i < sto->nmixtypes; i++) {
    UnsrtELDestroy(sto->els[i]);
  }

  Free(sto->els);
  Free(sto->nodesbycode);
  Free(sto->pmat);
  Free(sto->ndyadstype);
  
  WtPopDestroy(sto->wtp);
  // MHp->storage itself should be Freed by MHProposalDestroy
}

/********************
   void MH_TNT10
   Attempts to do 10 TNT steps at once, but this seems flawed currently
   because it does not correctly update network quantities like nedges
   after each of the 10 proposed toggles.
***********************/
MH_P_FN(MH_TNT10)
{
  /* *** don't forget tail-> head now */
  
  Edge nedges=EDGECOUNT(nwp);
  static double P=0.5;
  static double Q, DP, DO;

  if(MHp->ntoggles == 0) { /* Initialize */
    MHp->ntoggles=10;
    Q = 1-P;
    DP = P*DYADCOUNT(nwp);
    DO = DP/Q;
    return;
  }

  double logratio = 0;
  BD_LOOP({
      logratio = 0;
      for(unsigned int n = 0; n < 10; n++){
	if (unif_rand() < P && nedges > 0) { /* Select a tie at random */
	  GetRandEdge(Mtail, Mhead, nwp);
	  logratio += TNT_LR_E(nedges, Q, DP, DO);
	}else{ /* Select a dyad at random */
	  GetRandDyad(Mtail+n, Mhead+n, nwp);
	  if(IS_OUTEDGE(Mtail[n],Mhead[n])!=0){
	    logratio += TNT_LR_DE(nedges, Q, DP, DO);
	  }else{
	    logratio += TNT_LR_DN(nedges, Q, DP, DO);
	  }
	} 
      }
    });
  MHp->logratio += logratio;
}

/*********************
 void MH_constantedges
 propose pairs of toggles that keep number of edges
 the same.  This is done by (a) choosing an existing edge
 at random; (b) repeatedly choosing dyads at random until 
 one is found that does not have an edge; and (c) proposing
 toggling both these dyads.  Note that step (b) will be very 
 inefficient if the network is nearly complete, so this proposal is
 NOT recommended for such networks.  However, most network
 datasets are sparse, so this is not likely to be an issue.
*********************/
MH_P_FN(MH_ConstantEdges){  
  /* *** don't forget tail-> head now */
  
  if(MHp->ntoggles == 0) { /* Initialize */
    if(nwp->nedges==0 || nwp->nedges==DYADCOUNT(nwp)) MHp->ntoggles=MH_FAILED; /* Empty or full network. */
    else MHp->ntoggles=2;
    return;
  }
  /* Note:  This proposal cannot be used for full or empty observed graphs.
     If desired, we could check for this at initialization phase. 
     (For now, however, no way to easily return an error message and stop.)*/
  BD_LOOP({
      /* First, select edge at random */
      GetRandEdge(Mtail, Mhead, nwp);
      /* Second, select non-edge at random */
      GetRandNonedge(Mtail+1, Mhead+1, nwp);
    });
}

/*********************
 void MH_CondDegreeDist
 It used to be called  MH_CondDegDistSwapToggles
*********************/
MH_P_FN(MH_CondDegreeDist){  
  int noutedge=0, ninedge=0, k, fvalid;
  int k0, j0, j1, k1;
  int j0h, j1h;
  int trynode;
  Vertex e, alter, tail=0, head, head1;
  
  if(MHp->ntoggles == 0) { /* Initialize */
    MHp->ntoggles=2;    
    return;
  }

  fvalid = 0;
  trynode = 0;
  while(fvalid==0 && trynode < 500){

  trynode++;
  /* select a node at random */
  while(noutedge+ninedge==0){
    /* select a node at random */
    tail = 1 + unif_rand() * N_NODES;
    ninedge  = nwp->indegree[tail];
    noutedge = nwp->outdegree[tail];
  }

  /* choose a edge of the node at random */
    /* *** don't forget tail-> head now */

  k0 = (int)(unif_rand() * (noutedge+ninedge)); 
  if (k0 < noutedge){
    k=0;
    for(e = EdgetreeMinimum(nwp->outedges, tail);
    ((head = nwp->outedges[e].value) != 0 && k<k0);
    e = EdgetreeSuccessor(nwp->outedges, e)){++k;}
  }else{
    k=0;
    for(e = EdgetreeMinimum(nwp->inedges, tail);
    ((head = nwp->inedges[e].value) != 0 && k<(k0-noutedge));
    e = EdgetreeSuccessor(nwp->inedges, e)){++k;}
  }

  if ( (!DIRECTED && tail > head) ||
  (DIRECTED && k0 >= noutedge) ) {
    Mtail[0] = head;
    Mhead[0] = tail;
  }else{
    Mtail[0] = tail;
    Mhead[0] = head;
  }
  
  k1=0;
  fvalid=0;
  while(fvalid==0 && k1 < 100){
    while((alter = 1 + unif_rand() * N_NODES) == tail);
    fvalid=1;
    if(alter == head){fvalid=0;}
    if (k0 < noutedge || !DIRECTED){
      for(e = EdgetreeMinimum(nwp->outedges, tail);
      (fvalid==1 && ((head1 = nwp->outedges[e].value) != 0));
      e = EdgetreeSuccessor(nwp->outedges, e)){
        if(alter==head1){fvalid=0;}}
    }
    if (k0 >= noutedge || !DIRECTED){
      for(e = EdgetreeMinimum(nwp->inedges, tail);
      (fvalid==1 && ((head1 = nwp->inedges[e].value) != 0));
      e = EdgetreeSuccessor(nwp->inedges, e)){
        if(alter==head1){fvalid=0;}}
    }
    k1++;
  }

  if (k1 == 100){
    fvalid=0;
    continue;
  }
  
  if ( (!DIRECTED && alter > tail) ||
       (DIRECTED && k0 < noutedge) )
    {
      Mtail[1] = tail;
      Mhead[1] = alter;
    }else{
      Mtail[1] = alter;
      Mhead[1] = tail;
    }
  
  if (!DIRECTED){
    /* Check undirected degrees */
    k0 =nwp->outdegree[tail]  + nwp->indegree[tail];
    j0h=nwp->outdegree[head]  + nwp->indegree[head];
    j1h=nwp->outdegree[alter] + nwp->indegree[alter];
    
    j0=j0h-1;
    j1=j1h+1;
    
    if( ( (j0==j1h) && (j1==j0h) ) ){
      fvalid = 1;
    }else{
      fvalid = 0;
    }
  }else{
    /* Check directed degrees */
   if(k0 < noutedge){
     /* Check indegrees */
     j0h=nwp->indegree[head];
     j1h=nwp->indegree[alter];
   }else{
     /* Check outdegrees */
     j0h=nwp->outdegree[head];
     j1h=nwp->outdegree[alter];
   }
   j0=j0h-1;
   j1=j1h+1;
   
   if( ( (j0==j1h) && (j1==j0h) ) ){
     fvalid = 1;
   }else{
     fvalid = 0;
   }
  }
  
  }

  if (trynode==500){
    Mtail[1] = Mtail[0];
    Mhead[1] = Mhead[0];
  }
}

/*********************
 void MH_CondOutDegreeDist
*********************/
MH_P_FN(MH_CondOutDegreeDist){  
  int noutedge=0, k, fvalid=0;
  int k0, k1;
  int trynode;
  Vertex e, alter, tail=0, head, head1;
  
  if(MHp->ntoggles == 0) { /* Initialize */
    MHp->ntoggles=2;    
    return;
  }

  fvalid = 0;
  trynode = 0;
  while(fvalid==0 && trynode < 1500){

  trynode++;

  while(noutedge==0){
    /* select a node at random */
    tail = 1 + unif_rand() * N_NODES;
    noutedge = nwp->outdegree[tail];
  }
  
  k0 = (int)(unif_rand() * noutedge); 
  k=0;
  for(e = EdgetreeMinimum(nwp->outedges, tail);
      ((head = nwp->outedges[e].value) != 0 && k<k0);
      e = EdgetreeSuccessor(nwp->outedges, e)){++k;}
  Mtail[0] = tail;
  Mhead[0] = head;
  
  k1=0;
  fvalid=0;
  while(fvalid==0 && k1 < 100){
    while((alter = 1 + unif_rand() * N_NODES) == tail);
    fvalid=1;
    if(alter == head){fvalid=0;}
    for(e = EdgetreeMinimum(nwp->outedges, tail);
	(fvalid==1 && ((head1 = nwp->outedges[e].value) != 0));
	e = EdgetreeSuccessor(nwp->outedges, e)){
      if(alter==head1){fvalid=0;}}
    k1++;
  }
  if (k1 == 100){
    fvalid=0;
    continue;
  }
  
  Mtail[1] = tail;
  Mhead[1] = alter;
  }
  
  if(trynode==1500 || !CheckTogglesValid(MHp, nwp)){
      Mtail[0] = 1;
      Mhead[0] = 2;
      Mtail[1] = 1;
      Mhead[1] = 2;
  }
  

}

/*********************
 void MH_CondInDegreeDist
*********************/
MH_P_FN(MH_CondInDegreeDist){  
  int ninedge=0, k, fvalid=0;
  int k0, k1;
  int trynode;
  Vertex e, alter, tail=0, head, head1;

  /* *** don't forget tail-> head now */

  
  if(MHp->ntoggles == 0) { /* Initialize */
    MHp->ntoggles=2;    
    return;
  }

  fvalid = 0;
  trynode = 0;
  while(fvalid==0 && trynode < 1500){

  trynode++;

  while(ninedge==0){
    /* select a node at random */
    tail = 1 + unif_rand() * N_NODES;
    ninedge = nwp->indegree[tail];
  }
  
  k0 = (int)(unif_rand() * ninedge); 
  k=0;
  for(e = EdgetreeMinimum(nwp->inedges, tail);
      ((head = nwp->inedges[e].value) != 0 && k<k0);
      e = EdgetreeSuccessor(nwp->inedges, e)){++k;}
  Mtail[0] = head;
  Mhead[0] = tail;
  
  k1=0;
  fvalid=0;
  while(fvalid==0 && k1 < 100){
    while((alter = 1 + unif_rand() * N_NODES) == tail);
    fvalid=1;
    if(alter == head){fvalid=0;}
    for(e = EdgetreeMinimum(nwp->inedges, tail);
	(fvalid==1 && ((head1 = nwp->inedges[e].value) != 0));
	e = EdgetreeSuccessor(nwp->inedges, e)){
      if(alter==head1){fvalid=0;}}
    k1++;
  }
  if (k1 == 100){
    fvalid=0;
    continue;
  }
  
  Mtail[1] = alter;
  Mhead[1] = tail;
  
  }
  
  if(trynode==1500){
      Mtail[0] = 1;
      Mhead[0] = 2;
      Mtail[1] = 1;
      Mhead[1] = 2;
  }
}

/*********************
 void MH_TwoRandomToggles
*********************/
MH_P_FN(MH_TwoRandomToggles){  
  Vertex tail, head;
  int i;

  /* *** don't forget tail-> head now */
  
  if(MHp->ntoggles == 0) { /* Initialize OneRandomToggle */
    MHp->ntoggles=2;
    return;
  }

  for (i = 0; i < 2; i++){
   tail = 1 + unif_rand() * N_NODES;
   while ((head = 1 + unif_rand() * N_NODES) == tail);
   if (!DIRECTED && tail > head) {
     Mtail[i] = head;
     Mhead[i] = tail;
   }else{
     Mtail[i] = tail;
     Mhead[i] = head;
   }
  }
}

/*********************
 void MH_RandomNode
*********************/
MH_P_FN(MH_randomnode){
  
  Vertex root, alter;
  int j;
  
  if(MHp->ntoggles == 0) { /* Initialize OneRandomToggle */
    MHp->ntoggles= N_NODES - 1;
    return;
  }

  root = 1 + unif_rand() * N_NODES;
  
  j = 0;
  for (alter = 1; alter <= N_NODES; alter++)
    {
      /* there is never an edge (root, root) */
      if (alter != root) {
       if (!DIRECTED && root > alter) {
        Mtail[j] = alter;
        Mhead[j] = root;
       }else{
        Mtail[j] = root;
        Mhead[j] = alter;
       }
       j++;
      }
    }
}

/* The ones below have not been tested */

/*********************
 void MH_ConstrainedCondOutDegDist
*********************/
MH_P_FN(MH_ConstrainedCondOutDegDist){  
  int noutedge=0, k, fvalid=0;
  int k0, k1;
  Vertex e, alter, tail, head, head1;

  /* *** don't forget tail-> head now */

  while(noutedge==0){
    /* select a node at random */
    tail = 1 + unif_rand() * N_NODES;
    noutedge = nwp->outdegree[tail];
  }
  
  k0 = (int)(unif_rand() * noutedge); 
  k=0;
  for(e = EdgetreeMinimum(nwp->outedges, tail);
      ((head = nwp->outedges[e].value) != 0 && k<k0);
      e = EdgetreeSuccessor(nwp->outedges, e)){++k;}
  Mtail[0] = tail;
  Mhead[0] = head;
  
  k1=0;
  fvalid=0;
  while(fvalid==0 && k1 < 100){
    while((alter = 1 + unif_rand() * N_NODES) == tail);
    fvalid=1;
    if(alter == head){fvalid=0;}
    for(e = EdgetreeMinimum(nwp->outedges, tail);
	(fvalid==1 && ((head1 = nwp->outedges[e].value) != 0));
	e = EdgetreeSuccessor(nwp->outedges, e)){
      if(alter==head1){fvalid=0;}}
    k1++;
  }
  if (k1 == 100){
    Mtail[0] = Mhead[0] = 0;
    Mtail[1] = Mhead[1] = 0;
  }
  
  Mtail[1] = tail;
  Mhead[1] = alter;
  
  if (!fvalid){
    Mtail[0] = Mhead[0] = 0;
    Mtail[1] = Mhead[1] = 0;
  }
  
  for(k=0; k < 2; k++){
    if (dEdgeListSearch(Mtail[k], Mhead[k], MH_INPUTS)==0){
      Mtail[0] = Mhead[0] = 0;
      Mtail[1] = Mhead[1] = 0;
    }
  }
}


MH_P_FN(MH_NodePairedTiesToggles){  
  /* chooses a node and toggles all ties and
	 and toggles an equal number of matching nonties
	 for that node */
  int nedge=0,j,k;
  int fvalid = 1;
  Vertex e, tail, prop;

  /* *** don't forget tail-> head now */
  
  /* double to integer coercion */
  tail = 1 + unif_rand() * N_NODES; 
  
  for(e = EdgetreeMinimum(nwp->outedges, tail);
      (prop = nwp->outedges[e].value) != 0; /* loop if */
      e = EdgetreeSuccessor(nwp->outedges, e)) /* step through outedges of tail */
    {
      Mtail[nedge] = tail;
      Mhead[nedge] = prop;
      ++nedge;
    }
  for(e = EdgetreeMinimum(nwp->inedges, tail);
      (prop = nwp->inedges[e].value) != 0; /* loop if */
      e = EdgetreeSuccessor(nwp->inedges, e)) /* step through inedges of tail */
    {
      Mhead[nedge] = tail;
      Mtail[nedge] = prop;
      ++nedge;
    }
  
  if(nedge > N_NODES-nedge){
    Mtail[0] = Mhead[0] = 0;
    Mtail[1] = Mhead[1] = 0;
  }  
  j = 0;
  while (j <=nedge)
    {
      prop = 1 + unif_rand() * N_NODES; 
      k=0;
      fvalid=1;
      while(fvalid==1 && k<nedge+j){
	if(IS_OUTEDGE( MIN(prop,Mtail[k]),
		       MAX(prop,Mtail[k])) +
	   IS_OUTEDGE( MIN(prop,Mhead[k]),
		       MAX(prop,Mhead[k]))==0
	   ){++k;
	}else{
	  fvalid=0;
	}
      }
      if(prop>tail){
	Mtail[j+nedge] = tail;
	Mhead[j+nedge] = prop;
      }else{
	Mtail[j+nedge] = prop;
	Mhead[j+nedge] = tail;
      }
      ++j;
    }
  
  j = 2*nedge;
  if (!CheckTogglesValid(MHp, nwp))
    {
      *Mtail = *Mhead = 0;
    }
}

/*********************
 void MH_OneRandomTnTNode
*********************/
MH_P_FN(MH_OneRandomTnTNode){  
  Vertex tail=0, head, e, head1;
  int noutedge=0, ninedge=0, k0=0, fvalid=0, k;
  /* int ndyad; */

  /* *** don't forget tail-> head now */
  
  /* if ( DIRECTED )
    {
      ndyad = (N_NODES - 1) * N_NODES;
    }else{
      ndyad = (N_NODES - 1) * N_NODES / 2;
    } */

  double logratio=0;
  fvalid=0;
  while(fvalid==0){
    
    if ( unif_rand() < 0.5 && EDGECOUNT(nwp) > 0) 
      {
	
	/* select a tie */
	ninedge=0;
	noutedge=0;
	while(noutedge+ninedge==0){
	  /* select a node at random */
	  tail = 1 + unif_rand() * N_NODES;
	  ninedge = nwp->indegree[tail];
	  noutedge = nwp->outdegree[tail];
	}
	
	k0 = (int)(unif_rand() * (noutedge+ninedge)); 
	if (k0 < noutedge){
	  k=0;
	  for(e = EdgetreeMinimum(nwp->outedges, tail);
	      ((head = nwp->outedges[e].value) != 0 && k<k0);
	      e = EdgetreeSuccessor(nwp->outedges, e)){++k;}
	}else{
	  k=0;
	  for(e = EdgetreeMinimum(nwp->inedges, tail);
	      ((head = nwp->inedges[e].value) != 0 && k<(k0-noutedge));
	      e = EdgetreeSuccessor(nwp->inedges, e)){++k;}
	}
	if ( (!DIRECTED && tail > head) ||
	     (DIRECTED && k0 >= noutedge) )
	  {
	    Mtail[0] = head;
	    Mhead[0] = tail;
	  }else{
	    Mtail[0] = tail;
	    Mhead[0] = head;
	  }
	
	logratio = log(((noutedge+ninedge)*1.0)/(N_NODES-1-noutedge-ninedge-1));
	fvalid =1;
      }else{
	/* Choose random non-tie */

	/* select a node at random */
	ninedge=N_NODES-1;
	noutedge=0;
	while(noutedge+ninedge>=(N_NODES-1)){
	  ninedge=0;
	  /* select a node at random */
	  tail = 1 + unif_rand() * N_NODES;
	  ninedge = nwp->indegree[tail];
	  noutedge = nwp->outdegree[tail];
	}
	
	fvalid=0;
	while(fvalid==0){
	  while ((head = 1 + unif_rand() * N_NODES) == tail);
	  fvalid=1;
	  for(e = EdgetreeMinimum(nwp->outedges, tail);
	      (fvalid==1 && ((head1 = nwp->outedges[e].value) != 0));
	      e = EdgetreeSuccessor(nwp->outedges, e)){
	    if(head==head1){fvalid=0;}}
	  if (!(DIRECTED)){
	    for(e = EdgetreeMinimum(nwp->inedges, tail);
		(fvalid==1 && ((head1 = nwp->inedges[e].value) != 0));
		e = EdgetreeSuccessor(nwp->inedges, e)){
	      if(head==head1){fvalid=0;}}
	  }
	}
	
	if ( (!DIRECTED && tail > head) ||
	     (DIRECTED && k0 >= noutedge) )
	  {
	    Mtail[0] = head;
	    Mhead[0] = tail;
	  }else{
	    Mtail[0] = tail;
	    Mhead[0] = head;
	  }
	
        if ( DIRECTED )
	  {
	    logratio = log((N_NODES-1-noutedge-ninedge)/(noutedge+ninedge+1.0));
	  }else{
	    logratio = log((N_NODES-1-noutedge-ninedge)/(noutedge+ninedge+1.0));
	  }
      }
  }
  MHp->logratio += logratio;
}

/*********************
 void MH_ReallocateWithReplacement
*********************/
MH_P_FN(MH_ReallocateWithReplacement){  
  int i;
  Vertex root;
  Vertex* edges;
  int edgecount = 0;
  
  /* select a node at random */
  root = 1 + unif_rand() * N_NODES;

  edges = (Vertex *) Calloc(N_NODES+1, Vertex);
  for (i = 0; i <= N_NODES; i++)
    edges[i] = NO_EDGE;
  
  /* count current edges and mark them in an array */
  for (i = 1; i <= N_NODES; i++)
    {
      if (root == i) continue;
      if (IS_OUTEDGE(root, i) > 0)
	{
	  edges[i] = OLD_EDGE;
	  edgecount++;
	}
      if (!DIRECTED && (root > i) &&
	  (IS_OUTEDGE(i, root) > 0))
	{
	  edges[i] = OLD_EDGE;
	  edgecount++;
	}
    }
  
  /* select edgecount edges to create */
  for (i = 0; i < edgecount; i++)
    {
      Vertex newhead;
      /* get a new edge, neither the root nor something already chosen */
      while ((newhead = 1 + unif_rand() * N_NODES) == root ||
	     (edges[newhead] & NEW_EDGE))
	;
      
      /* if this edge already exists - (OLD_EDGE | NEW_EDGE) == CAN_IGNORE */
      edges[newhead] = edges[newhead] | NEW_EDGE;
    }
  
  /* index into Mtail/Mhead is  */
  edgecount = 0;
  
  /* add to toggle list:  anything that is non zero in edges array
     should be toggled, whether on or off. */
  for (i = 0; i <= N_NODES; i++)
    {
      if (edges[i] == NO_EDGE || edges[i] == CAN_IGNORE) continue;
      
      /* double to integer coercion */
      Mtail[edgecount] = root;
      Mhead[edgecount] = i;
      
      if (!DIRECTED && (Mtail[edgecount] > Mhead[edgecount]))
	{
	  Vertex temp;
	  temp = Mtail[edgecount];
	  Mtail[edgecount] = Mhead[edgecount];
	  Mhead[edgecount] = temp;
	}
      edgecount++;
    }
  Free(edges);
}

/*********************
 void MH_AllTogglesForOneNode
*********************/
MH_P_FN(MH_AllTogglesForOneNode){
  
  int i;
  int j;
  int root;
  
  root = 1 + unif_rand() * N_NODES;
  
  j = 0;
  for (i = 1; i <= N_NODES; i++)
    {
      /* probability here only do this with .8? */
      
      /* there is never an edge (root, root) */
      if (i == root)
	continue;
      
      /* double to integer coercion */
      Mtail[j] = root;
      Mhead[j] = i;
      
      if (!DIRECTED && (Mtail[j] > Mhead[j]))
	{
	  Vertex temp;
	  temp = Mtail[j];
	  Mtail[j] = Mhead[j];
	  Mhead[j] = temp;
	}
      j++;
    }
}


/*********************
 void MH_SwitchLabelTwoNodesToggles
*********************/
MH_P_FN(MH_SwitchLabelTwoNodesToggles){  
  int nedge1=0, nedge2=0, k, ntoggles;
  Vertex *edges1, *edges2;
  Vertex e, tail2, head2, tail1, head1;

  /* *** don't forget tail-> head now */
  
  /* select a node at random */
  edges1 = (Vertex *) Calloc(N_NODES+1, Vertex);
  edges2 = (Vertex *) Calloc(N_NODES+1, Vertex);
  
  while(nedge1==0){
    tail1 = 1 + unif_rand() * N_NODES;
    
    for(e = EdgetreeMinimum(nwp->outedges, tail1);
	(head1 = nwp->outedges[e].value) != 0; /* loop if */
	e = EdgetreeSuccessor(nwp->outedges, e)) /* step through outedges of tail1 */
      {
        edges1[nedge1] = head1;
	++nedge1;
      }
    for(e = EdgetreeMinimum(nwp->inedges, tail1);
	(head1 = nwp->inedges[e].value) != 0; /* loop if */
	e = EdgetreeSuccessor(nwp->inedges, e)) /* step through inedges of tail1 */
      {
        edges1[nedge1] = head1;
	++nedge1;
      }
  }
  
  while((tail2 = 1 + unif_rand() * N_NODES) == tail1);
  
  for(e = EdgetreeMinimum(nwp->outedges, tail2);
      (head2 = nwp->outedges[e].value) != 0; /* loop if */
      e = EdgetreeSuccessor(nwp->outedges, e)) /* step through outedges of tail2 */
    {
      edges2[nedge2] = head2;
      ++nedge2;
    }
  for(e = EdgetreeMinimum(nwp->inedges, tail2);
      (head2 = nwp->inedges[e].value) != 0; /* loop if */
      e = EdgetreeSuccessor(nwp->inedges, e)) /* step through inedges of tail2 */
    {
      edges2[nedge2] = head2;
      ++nedge2;
    }
  
  ntoggles = 0;
  for(k=0; k < nedge1; k++){
    if (tail1 > edges1[k])
      {
	Mtail[ntoggles] = edges1[k];
	Mhead[ntoggles] = tail1;
      }
    if (tail1 < edges1[k]){
      Mtail[ntoggles] = tail1;
      Mhead[ntoggles] = edges1[k];
    }
    if(tail1 != edges1[k]) ntoggles++;
  }
  
  for(k=0; k < nedge2; k++){
    if (tail1 > edges2[k])
      {
	Mtail[ntoggles] = edges2[k];
	Mhead[ntoggles] = tail1;
      }
    if (tail1 < edges2[k]){
      Mtail[ntoggles] = tail1;
      Mhead[ntoggles] = edges2[k];
    }
    if(tail1 != edges2[k]) ntoggles++;
  }
  
  for(k=0; k < nedge2; k++){
    if (tail2 > edges2[k])
      {
	Mtail[ntoggles] = edges2[k];
	Mhead[ntoggles] = tail2;
      }
    if (tail2 < edges2[k]){
      Mtail[ntoggles] = tail2;
      Mhead[ntoggles] = edges2[k];
    }
    if(tail2 != edges2[k]) ntoggles++;
  }
  
  for(k=0; k < nedge1; k++){
    if (tail2 > edges1[k])
      {
	Mtail[ntoggles] = edges1[k];
	Mhead[ntoggles] = tail2;
      }
    if (tail2 < edges1[k]){
      Mtail[ntoggles] = tail2;
      Mhead[ntoggles] = edges1[k];
    }
    if(tail2 != edges1[k]) ntoggles++;
  }
  Free(edges1);
  Free(edges2);
}


/*********************
 void MH_ConstrainedCondDegDist
*********************/
MH_P_FN(MH_ConstrainedCondDegDist){  
  int noutedge=0, ninedge=0, k, fvalid=0;
  int k0, j0, j1, k1;
  int j0h, j1h;
  Vertex *outedges, *inedges;
  Vertex e, alter, tail=0, head;

  /* *** don't forget tail-> head now */
  
  /* select a node at random */
  outedges = (Vertex *) Calloc(N_NODES+1, Vertex);
  inedges = (Vertex *) Calloc(N_NODES+1, Vertex);
  
  while(noutedge==0 && ninedge==0){
    tail = 1 + unif_rand() * N_NODES;
    
    for(e = EdgetreeMinimum(nwp->outedges, tail);
	(head = nwp->outedges[e].value) != 0; /* loop if */
	e = EdgetreeSuccessor(nwp->outedges, e)) /* step through outedges of tail */
      {
        outedges[noutedge] = head;
	++noutedge;
      }
    for(e = EdgetreeMinimum(nwp->inedges, tail);
	(head = nwp->inedges[e].value) != 0; /* loop if */
	e = EdgetreeSuccessor(nwp->inedges, e)) /* step through inedges of tail */
      {
        inedges[ninedge] = head;
	++ninedge;
      }
  }
  
  k0 = (int)(unif_rand() * (noutedge+ninedge)); 
  if (k0 < noutedge){
    head = outedges[k0]; 
  }else{
    head = inedges[k0-noutedge]; 
  }
  if ( (!DIRECTED && tail > head) ||
       (  DIRECTED  && k0 >= noutedge) )
    {
      Mtail[0] = head;
      Mhead[0] = tail;
    }else{
      Mtail[0] = tail;
      Mhead[0] = head;
    }
  
  if (dEdgeListSearch(Mtail[0], Mhead[0], MH_INPUTS)==0){
    Mtail[0] = Mhead[0] = 0;
    Mtail[1] = Mhead[1] = 0;
  }
  
  fvalid=0;
  k1=0;
  while(fvalid==0 && k1 < 100){
    while((alter = 1 + unif_rand() * N_NODES) == tail);
    if(alter != head){fvalid=1;}
    fvalid=1;
    if (k0 < noutedge || !(DIRECTED)){
      k=0;
      while(fvalid==1 && noutedge > 0 && k <= noutedge-1){
	if(alter == outedges[k]){fvalid=0;}else{++k;}
      }
    }
    if (k0 >= noutedge || !(DIRECTED)){
      k=0;
      while(fvalid==1 && ninedge > 0 && k <= ninedge-1){
	if(alter == inedges[k]){fvalid=0;}else{++k;}
      }
    }
    k1++;
  }
  
  if (k1 == 100){
    Mtail[0] = Mhead[0] = 0;
    Mtail[1] = Mhead[1] = 0;
  }
  
  if ( (!DIRECTED && alter > tail) ||
       (DIRECTED && k0 < noutedge) )
    {
      Mtail[1] = tail;
      Mhead[1] = alter;
    }else{
      Mtail[1] = alter;
      Mhead[1] = tail;
    }
  
  if (dEdgeListSearch(Mtail[1], Mhead[1], MH_INPUTS)==0){
    Mtail[0] = Mhead[0] = 0;
    Mtail[1] = Mhead[1] = 0;
  }
  
  Free(outedges);
  Free(inedges);
  
  /* Check undirected degrees */

  /* *** don't forget tail-> head now */

  if (!DIRECTED){
    k0=nwp->outdegree[tail]+ nwp->indegree[tail];
    j0h=nwp->outdegree[head]+ nwp->indegree[head];
    j1h=nwp->outdegree[alter]+ nwp->indegree[alter];
    
    j0=j0h-1;
    j1=j1h+1;
    
    if( ( (j0==j1h) && (j1==j0h) ) ){
      fvalid = 1;
    }else{
      fvalid = 0;
    }
  }else{
    if(k0 < noutedge){
      /* Check indegrees */
      j0h=nwp->indegree[head];
      j1h=nwp->indegree[alter];
    }else{
      /* Check outdegrees */
      j0h=nwp->outdegree[head];
      j1h=nwp->outdegree[alter];
    }
    j0=j0h-1;
    j1=j1h+1;
    
    if( ( (j0==j1h) && (j1==j0h) ) ){
      fvalid = 1;
    }else{
      fvalid = 0;
    }
  }
  
  if (!fvalid){
    Mtail[0] = Mhead[0] = 0;
    Mtail[1] = Mhead[1] = 0;
  }
}

void MH_ConstrainedNodePairedTiesToggles (MHProposal *MHp,
       	 Network *nwp) {  
  /* chooses a node and toggles all ties and
     and toggles an equal number of matching nonties
     for that node */
  int nedge=0,j,k;
  int fvalid = 1;
  Vertex e, tail, prop;

  /* *** don't forget tail-> head now */
  
  /* double to integer coercion */
  tail = 1 + unif_rand() * N_NODES; 
  
  for(e = EdgetreeMinimum(nwp->outedges, tail);
      (prop = nwp->outedges[e].value) != 0; /* loop if */
      e = EdgetreeSuccessor(nwp->outedges, e)) /* step through outedges of tail */
    {
      Mtail[nedge] = tail;
      Mhead[nedge] = prop;
      ++nedge;
    }
  for(e = EdgetreeMinimum(nwp->inedges, tail);
      (prop = nwp->inedges[e].value) != 0; /* loop if */
      e = EdgetreeSuccessor(nwp->inedges, e)) /* step through inedges of tail */
    {
      Mhead[nedge] = tail;
      Mtail[nedge] = prop;
      ++nedge;
    }
  
  if(nedge > N_NODES-nedge){
    Mtail[0] = Mhead[0] = 0;
    Mtail[1] = Mhead[1] = 0;
  }  
  j = 0;
  while (j <=nedge)
    {
      prop = 1 + unif_rand() * N_NODES; 
      k=0;
      fvalid=1;
      while(fvalid==1 && k<nedge+j){
	if(IS_OUTEDGE(MIN(prop,Mtail[k]),
			   MAX(prop,Mtail[k])) +
	   IS_OUTEDGE( MIN(prop,Mhead[k]),
			   MAX(prop,Mhead[k]))==0
	   ){++k;
	}else{
	  fvalid=0;}
      }
      if(prop>tail){
	Mtail[j+nedge] = tail;
	Mhead[j+nedge] = prop;
      }else{
	Mtail[j+nedge] = prop;
	Mhead[j+nedge] = tail;
      }
      ++j;
    }
  
  j = 2*nedge;
  if (!CheckConstrainedTogglesValid(MHp, nwp))
    {
      *Mtail = *Mhead = 0;
    }
}

/*********************
 void MH_ConstrainedReallocateWithReplacement
*********************/
void MH_ConstrainedReallocateWithReplacement (MHProposal *MHp,
       	 Network *nwp) {  
  int i;
  Vertex root;
  Vertex* edges;
  int edgecount = 0;
  
  /* select a node at random */
  root = 1 + unif_rand() * N_NODES;

  edges = (Vertex *) Calloc(N_NODES+1, Vertex);
  for (i = 0; i <= N_NODES; i++)
    edges[i] = NO_EDGE;
  
  /* count current edges and mark them in an array */
  for (i = 1; i <= N_NODES; i++)
    {
      if (root == i) continue;
      if (IS_OUTEDGE(root, i) > 0)
	{
	  edges[i] = OLD_EDGE;
	  edgecount++;
	}
      if (!DIRECTED && (root > i) &&
	  (IS_OUTEDGE(i, root) > 0))
	{
	  edges[i] = OLD_EDGE;
	  edgecount++;
	}
    }
  
  /* select edgecount edges to create */
  for (i = 0; i < edgecount; i++)
    {
      Vertex newhead;
      
      /* get a new edge, neither the root nor something already chosen */
      while ((newhead = 1 + unif_rand() * N_NODES) == root ||
	     (edges[newhead] & NEW_EDGE))
	;
      
      /* if this edge already exists - (OLD_EDGE | NEW_EDGE) == CAN_IGNORE */
      edges[newhead] = edges[newhead] | NEW_EDGE;
    }
  
  /* index into Mtail/Mhead is  */
  edgecount = 0;
  
  /* add to toggle list:  anything that is non zero in edges array
     should be toggled, whether on or off. */
  for (i = 0; i <= N_NODES; i++)
    {
      if (edges[i] == NO_EDGE || edges[i] == CAN_IGNORE) continue;
      
      /* double to integer coercion */
      Mtail[edgecount] = root;
      Mhead[edgecount] = i;
      
      if (!DIRECTED && (Mtail[edgecount] > Mhead[edgecount]))
	{
	  Vertex temp;
	  temp = Mtail[edgecount];
	  Mtail[edgecount] = Mhead[edgecount];
	  Mhead[edgecount] = temp;
	}
      edgecount++;
    }
  Free(edges);
}

/*********************
 void MH_ConstrainedAllTogglesForOneNode
*********************/
void MH_ConstrainedAllTogglesForOneNode (MHProposal *MHp,
					 Network *nwp) {
  int i;
  int j;
  int root;
  
  root = 1 + unif_rand() * N_NODES;
  
  j = 0;
  for (i = 1; i <= N_NODES; i++)
    {
      /* probability here only do this with .8? */
      
      /* there is never an edge (root, root) */
      if (i == root)
	continue;
      
      /* double to integer coercion */
      Mtail[j] = root;
      Mhead[j] = i;
      
      if (!DIRECTED && (Mtail[j] > Mhead[j]))
	{
	  Vertex temp;
	  temp = Mtail[j];
	  Mtail[j] = Mhead[j];
	  Mhead[j] = temp;
	}
      j++;
    }
}

/*********************
 void MH_ConstrainedTwoRandomToggles
*********************/
void MH_ConstrainedTwoRandomToggles (MHProposal *MHp,
				 Network *nwp) {  
  int i;
  
  for (i = 0; i < 2; i++)
    {
      /* double to integer coercion */
      Mtail[i] = 1 + unif_rand() * N_NODES; 
      while ((Mhead[i] = 1 + unif_rand() * N_NODES) == Mtail[i]);
      
      while(dEdgeListSearch(Mtail[i], Mhead[i], MH_INPUTS)==0){
	Mtail[i] = 1 + unif_rand() * N_NODES; 
	while ((Mhead[i] = 1 + unif_rand() * N_NODES) == Mtail[i]);
      }
      if (!DIRECTED && Mtail[i] > Mhead[i]) 
	{
	  Vertex temp;
	  temp = Mtail[i];
	  Mtail[i] = Mhead[i];
	  Mhead[i] = temp;
	}
    }
  
  if (!CheckConstrainedTogglesValid(MHp, nwp))
    {
      Mtail[0] = Mhead[0] = 0;
      Mtail[1] = Mhead[1] = 0;
    }  
}

/*********************
 void MH_ConstrainedCondDeg
*********************/
void MH_ConstrainedCondDeg (MHProposal *MHp,
					 Network *nwp) {  
  /* WARNING: THIS NEEDS TO BE FIXED */
  int nedge1=0, nedge2=0, k, toomany, fvalid=0;
  Vertex *edges1, *edges2;
  Vertex e, tail2=0, head2, tail1, head1;
  
  /* select a node at random */
  edges1 = (Vertex *) Calloc(N_NODES+1, Vertex);
  edges2 = (Vertex *) Calloc(N_NODES+1, Vertex);
  
  while(nedge1==0){
    tail1 = 1 + unif_rand() * N_NODES;
    
    for(e = EdgetreeMinimum(nwp->outedges, tail1);
	(head1 = nwp->outedges[e].value) != 0; /* loop if */
	e = EdgetreeSuccessor(nwp->outedges, e)) /* step through outedges of tail1 */
      {
        edges1[nedge1] = head1;
	++nedge1;
      }
    for(e = EdgetreeMinimum(nwp->inedges, tail1);
	(head1 = nwp->inedges[e].value) != 0; /* loop if */
	e = EdgetreeSuccessor(nwp->inedges, e)) /* step through inedges of tail1 */
      {
        edges1[nedge1] = head1;
	++nedge1;
      }
  }
  
  head1 = edges1[(int)(unif_rand() * nedge1)]; 
  if (tail1 > head1)
    {
      Mtail[0] = head1;
      Mhead[0] = tail1;
    }else{
      Mtail[0] = tail1;
      Mhead[0] = head1;
    }
   
  toomany = 0;
  while(nedge2==0 && toomany < 100){
    fvalid=0;
    while(fvalid==0){
      while((tail2 = 1 + unif_rand() * N_NODES) == tail1);
      k=0;
      fvalid=1;
      while(fvalid==1 && k < nedge1){
	if(tail2 == edges1[k]){fvalid=0;}else{++k;}
      }
    }

    for(e = EdgetreeMinimum(nwp->outedges, tail2);
	(head2 = nwp->outedges[e].value) != 0; /* loop if */
	e = EdgetreeSuccessor(nwp->outedges, e)) /* step through outedges of tail2 */
      {
        edges2[nedge2] = head2;
	++nedge2;
      }
    for(e = EdgetreeMinimum(nwp->inedges, tail2);
	(head2 = nwp->inedges[e].value) != 0; /* loop if */
	e = EdgetreeSuccessor(nwp->inedges, e)) /* step through inedges of tail2 */
      {
        edges2[nedge2] = head2;
	++nedge2;
      }
    ++toomany;
  }
  if (toomany==100){
    Mtail[0] = Mhead[0] = 0;
    Mtail[1] = Mhead[1] = 0;
  }
  toomany=0;
  fvalid=0;
  while(fvalid==0 && toomany < 10){
    while((head2 = edges2[(int)(unif_rand() * nedge2)]) == tail1);
    k=0;
    fvalid=1;
    while(fvalid==1 && k < nedge1){
      if(head2 == edges1[k]){fvalid=0;}else{++k;}
    }
    ++toomany;
  }
  if (!fvalid || toomany==10){
    Mtail[0] = Mhead[0] = 0;
    Mtail[1] = Mhead[1] = 0;
    Free(edges1);
    Free(edges2);
      }
  if (tail2 > head2)
    {
      Mtail[1] = head2;
      Mhead[1] = tail2;
    }else{
      Mtail[1] = tail2;
      Mhead[1] = head2;
    }
  Free(edges1);
  Free(edges2);
}

/*********************
 void MH_ConstrainedSwitchLabelTwoNodesToggles
*********************/
void MH_ConstrainedSwitchLabelTwoNodesToggles (MHProposal *MHp,
       	 Network *nwp)  {  
  int nedge1=0, nedge2=0, k, ntoggles;
  Vertex *edges1, *edges2;
  Vertex e, tail2, head2, tail1, head1;

  /* *** don't forget tail-> head now */
  
  /* select a node at random */

  edges1 = (Vertex *) Calloc(N_NODES+1, Vertex);
  edges2 = (Vertex *) Calloc(N_NODES+1, Vertex);

  while(nedge1==0){
    tail1 = 1 + unif_rand() * N_NODES;
    
    for(e = EdgetreeMinimum(nwp->outedges, tail1);
	(head1 = nwp->outedges[e].value) != 0; /* loop if */
	e = EdgetreeSuccessor(nwp->outedges, e)) /* step through outedges of tail1 */
      {
        edges1[nedge1] = head1;
	++nedge1;
      }
    for(e = EdgetreeMinimum(nwp->inedges, tail1);
	(head1 = nwp->inedges[e].value) != 0; /* loop if */
	e = EdgetreeSuccessor(nwp->inedges, e)) /* step through inedges of tail1 */
      {
        edges1[nedge1] = head1;
	++nedge1;
      }
  }
  
  while((tail2 = 1 + unif_rand() * N_NODES) == tail1);
  
  for(e = EdgetreeMinimum(nwp->outedges, tail2);
      (head2 = nwp->outedges[e].value) != 0; /* loop if */
      e = EdgetreeSuccessor(nwp->outedges, e)) /* step through outedges of tail2 */
    {
      edges2[nedge2] = head2;
      ++nedge2;
    }
  for(e = EdgetreeMinimum(nwp->inedges, tail2);
      (head2 = nwp->inedges[e].value) != 0; /* loop if */
      e = EdgetreeSuccessor(nwp->inedges, e)) /* step through inedges of tail2 */
    {
      edges2[nedge2] = head2;
      ++nedge2;
    }
  
  ntoggles = 0;
  for(k=0; k < nedge1; k++){
    if (tail1 > edges1[k])
      {
	Mtail[ntoggles] = edges1[k];
	Mhead[ntoggles] = tail1;
      }
    if (tail1 < edges1[k]){
      Mtail[ntoggles] = tail1;
      Mhead[ntoggles] = edges1[k];
    }
    if(tail1 != edges1[k]) ntoggles++;
  }
  
  for(k=0; k < nedge2; k++){
    if (tail1 > edges2[k])
      {
	Mtail[ntoggles] = edges2[k];
	Mhead[ntoggles] = tail1;
      }
    if (tail1 < edges2[k]){
      Mtail[ntoggles] = tail1;
      Mhead[ntoggles] = edges2[k];
    }
    if(tail1 != edges2[k]) ntoggles++;
  }
  
  for(k=0; k < nedge2; k++){
    if (tail2 > edges2[k])
      {
	Mtail[ntoggles] = edges2[k];
	Mhead[ntoggles] = tail2;
      }
    if (tail2 < edges2[k]){
      Mtail[ntoggles] = tail2;
      Mhead[ntoggles] = edges2[k];
    }
    if(tail2 != edges2[k]) ntoggles++;
  }
  
  for(k=0; k < nedge1; k++){
    if (tail2 > edges1[k])
      {
	Mtail[ntoggles] = edges1[k];
	Mhead[ntoggles] = tail2;
      }
    if (tail2 < edges1[k]){
      Mtail[ntoggles] = tail2;
      Mhead[ntoggles] = edges1[k];
    }
    if(tail2 != edges1[k]) ntoggles++;
  }
  Free(edges1);
  Free(edges2);
}

/*********************
 void MH_ConstantEdgesToggles
*********************/
MH_P_FN(MH_ConstantEdgesToggles){  
  int noutedge=0, ninedge=0, k, fvalid=0;
  int k0, k1;
  Vertex e, alter, tail, head, head1;

  /* *** don't forget tail-> head now */
  
  while(noutedge+ninedge==0){
    /* select a node at random */
    tail = 1 + unif_rand() * N_NODES;
    ninedge  = nwp->indegree[tail];
    noutedge = nwp->outdegree[tail];
  }
  
  k0 = (int)(unif_rand() * (noutedge+ninedge)); 
  if (k0 < noutedge){
    k=0;
    for(e = EdgetreeMinimum(nwp->outedges, tail);
	((head = nwp->outedges[e].value) != 0 && k<k0);
	e = EdgetreeSuccessor(nwp->outedges, e)){++k;}
  }else{
    k=0;
    for(e = EdgetreeMinimum(nwp->inedges, tail);
	((head = nwp->inedges[e].value) != 0 && k<(k0-noutedge));
	e = EdgetreeSuccessor(nwp->inedges, e)){++k;}
  }
  
  if ( (!DIRECTED && tail > head) ||
       (DIRECTED && k0 >= noutedge) )
    {
      Mtail[0] = head;
      Mhead[0] = tail;
    }else{
      Mtail[0] = tail;
      Mhead[0] = head;
    }
  
  k1=0;
  fvalid=0;
  while(fvalid==0 && k1 < 100){
    while((alter = 1 + unif_rand() * N_NODES) == tail);
    fvalid=1;
    if(alter == head){fvalid=0;}
    if (k0 < noutedge || !(DIRECTED)){
      for(e = EdgetreeMinimum(nwp->outedges, tail);
	  (fvalid==1 && ((head1 = nwp->outedges[e].value) != 0));
	  e = EdgetreeSuccessor(nwp->outedges, e)){
	if(alter==head1){fvalid=0;}}
    }
    if (k0 >= noutedge || !(DIRECTED)){
      for(e = EdgetreeMinimum(nwp->inedges, tail);
	  (fvalid==1 && ((head1 = nwp->inedges[e].value) != 0));
	  e = EdgetreeSuccessor(nwp->inedges, e)){
	if(alter==head1){fvalid=0;}}
    }
    k1++;
  }
  if (k1 == 100){
    Mtail[0] = Mhead[0] = 0;
    Mtail[1] = Mhead[1] = 0;
  }
  
  if ( (!DIRECTED && alter > tail) ||
       (DIRECTED && k0 < noutedge) )
    {
      Mtail[1] = tail;
      Mhead[1] = alter;
    }else{
      Mtail[1] = alter;
      Mhead[1] = tail;
    }
  
  if (!fvalid){
    Mtail[0] = Mhead[0] = 0;
    Mtail[1] = Mhead[1] = 0;
  }else{  
  }
}

/*********************
 void MH_CondDegSwitchToggles
*********************/
MH_P_FN(MH_CondDegSwitchToggles){  
  int noutedge, ninedge, i;
  int k, k0, toomany;
  Vertex e, tail, head;

  /* *** don't forget tail-> head now */
  
  /* select a node at random */
  for (i = 0; i < 2; i++){
    toomany=0;
    noutedge=0;
    ninedge=0;
    while(noutedge==0 && ninedge==0 && toomany < 100){
      tail = 1 + unif_rand() * N_NODES;
      ninedge=0;
      noutedge=0;
      while(noutedge+ninedge==0){
	/* select a node at random */
	tail = 1 + unif_rand() * N_NODES;
	ninedge = nwp->indegree[tail];
	noutedge = nwp->outdegree[tail];
      }
      ++toomany;
    }
    
    if (toomany == 100){
      Mtail[0] = Mhead[0] = 0;
      Mtail[1] = Mhead[1] = 0;
    }
    
    k0 = (int)(unif_rand() * (noutedge+ninedge)); 
    if (k0 < noutedge){
      k=0;
      for(e = EdgetreeMinimum(nwp->outedges, tail);
	  ((head = nwp->outedges[e].value) != 0 && k<k0);
	  e = EdgetreeSuccessor(nwp->outedges, e)){++k;}
    }else{
      k=0;
      for(e = EdgetreeMinimum(nwp->inedges, tail);
	  ((head = nwp->inedges[e].value) != 0 && k<(k0-noutedge));
	  e = EdgetreeSuccessor(nwp->inedges, e)){++k;}
    }
    if ( (!DIRECTED && tail > head) ||
	 (DIRECTED && k0 >= noutedge) )
      {
	Mtail[i] = head;
	Mhead[i] = tail;
      }else{
	Mtail[i] = tail;
	Mhead[i] = head;
      }
  }
  
  if (IS_OUTEDGE( Mtail[0],Mhead[1]) ||
      IS_OUTEDGE( Mtail[1],Mhead[0]) ){
    Mtail[0] = Mhead[0] = 0;
    Mtail[1] = Mhead[1] = 0;
  }
  
  if ( (!DIRECTED && Mtail[0] > Mhead[1]) )
    {
      Mtail[2] = Mhead[1];
      Mhead[2] = Mtail[0];
    }else{
      Mtail[2] = Mtail[0];
      Mhead[2] = Mhead[1];
    }
  
  if ( (!DIRECTED && Mtail[1] > Mhead[0]) )
    {
      Mtail[3] = Mhead[0];
      Mhead[3] = Mtail[1];
    }else{
      Mtail[3] = Mtail[1];
      Mhead[3] = Mhead[0];
    }
}


