//----------------------------------------------------------------------
// File:			kd_pr_search.cpp
// Programmer:		Sunil Arya and David Mount
// Description:		Priority search for kd-trees
// Last modified:	01/04/05 (Version 1.0)
//----------------------------------------------------------------------
// Copyright (c) 1997-2005 University of Maryland and Sunil Arya and
// David Mount.  All Rights Reserved.
// 
// This software and related documentation is part of the Approximate
// Nearest Neighbor Library (ANN).  This software is provided under
// the provisions of the Lesser GNU Public License (LGPL).  See the
// file ../ReadMe.txt for further information.
// 
// The University of Maryland (U.M.) and the authors make no
// representations about the suitability or fitness of this software for
// any purpose.  It is provided "as is" without express or implied
// warranty.
//----------------------------------------------------------------------
// History:
//	Revision 0.1  03/04/98
//		Initial release
//----------------------------------------------------------------------

#include "kd_pr_search.h"  // kd priority search declarations

#include <emmintrin.h>

using namespace ann_1_1_char;

//----------------------------------------------------------------------
//	Approximate nearest neighbor searching by priority search.
//		The kd-tree is searched for an approximate nearest neighbor.
//		The point is returned through one of the arguments, and the
//		distance returned is the SQUARED distance to this point.
//
//		The method used for searching the kd-tree is called priority
//		search.  (It is described in Arya and Mount, ``Algorithms for
//		fast vector quantization,'' Proc. of DCC '93: Data Compression
//		Conference}, eds. J. A. Storer and M. Cohn, IEEE Press, 1993,
//		381--390.)
//
//		The cell of the kd-tree containing the query point is located,
//		and cells are visited in increasing order of distance from the
//		query point.  This is done by placing each subtree which has
//		NOT been visited in a priority queue, according to the closest
//		distance of the corresponding enclosing rectangle from the
//		query point.  The search stops when the distance to the nearest
//		remaining rectangle exceeds the distance to the nearest point
//		seen by a factor of more than 1/(1+eps). (Implying that any
//		point found subsequently in the search cannot be closer by more
//		than this factor.)
//
//		The main entry point is annkPriSearch() which sets things up and
//		then call the recursive routine ann_pri_search().  This is a
//		recursive routine which performs the processing for one node in
//		the kd-tree.  There are two versions of this virtual procedure,
//		one for splitting nodes and one for leaves. When a splitting node
//		is visited, we determine which child to continue the search on
//		(the closer one), and insert the other child into the priority
//		queue.  When a leaf is visited, we compute the distances to the
//		points in the buckets, and update information on the closest
//		points.
//
//		Some trickery is used to incrementally update the distance from
//		a kd-tree rectangle to the query point.  This comes about from
//		the fact that which each successive split, only one component
//		(along the dimension that is split) of the squared distance to
//		the child rectangle is different from the squared distance to
//		the parent rectangle.
//----------------------------------------------------------------------

////----------------------------------------------------------------------
////		To keep argument lists short, a number of global variables
////		are maintained which are common to all the recursive calls.
////		These are given below.
////----------------------------------------------------------------------
//
//double			ann_1_1_char::ANNprEps;				// the error bound
//int				ann_1_1_char::ANNprDim;				// dimension of space
//ANNpoint		ann_1_1_char::ANNprQ;					// query point
//double			ann_1_1_char::ANNprMaxErr;			// max tolerable squared error
//ANNpointArray	ann_1_1_char::ANNprPts;				// the points
//ANNpr_queue		*ann_1_1_char::ANNprBoxPQ;			// priority queue for boxes
//ANNmin_k		*ann_1_1_char::ANNprPointMK;			// set of k closest points

#ifdef ANN_PERF
#undef ANN_PERF
#endif

//----------------------------------------------------------------------
//	annkPriSearch - priority search for k nearest neighbors
//----------------------------------------------------------------------

void ANNkd_tree::annkPriSearch(
	ANNpoint			q,				// query point
	int					k,				// number of near neighbors to return
	ANNidxArray			nn_idx,			// nearest neighbor indices (returned)
	ANNdistArray		dd,				// dist to near neighbors (returned)
	double				eps)			// error bound (ignored)
{
    // max tolerable squared error
    ANNprTempStore store;
    //printf("store address: %p\n", &store); fflush(stdout); //debug

    store.ANNprMaxErr = ANN_POW(1.0 + eps);
    // ANN_FLOP(2)							// increment floating ops
    
    store.ANNprDim = dim;						// copy arguments to static equivs
    store.ANNprQ = q;
    store.ANNprPts = pts;
    store.ANNptsVisited = 0;					// initialize count of points visited

    store.ANNprPointMK = new ANNmin_k(k);		// create set for closest k points
    //printf("store.ANNprPointMK address: %p\n", store.ANNprPointMK); fflush(stdout); //debug

    // distance to root box
    ANNdist box_dist = annBoxDistance(q, bnd_box_lo, bnd_box_hi, dim);

    store.ANNprBoxPQ = new ANNpr_queue(n_pts);// create priority queue for boxes
    store.ANNprBoxPQ->insert(box_dist, root); // insert root in priority queue

    while (store.ANNprBoxPQ->non_empty() &&
           (!(ANNmaxPtsVisited != 0 && store.ANNptsVisited > ANNmaxPtsVisited))) {
        ANNkd_ptr np;   // next box from prior queue
            
        // extract closest box from queue
        store.ANNprBoxPQ->extr_min(box_dist, (void *&) np);
            
        // ANN_FLOP(2)    // increment floating ops
        if (box_dist*store.ANNprMaxErr >= store.ANNprPointMK->max_key())
            break;

        if (!np->isLeaf)
            ((ANNkd_split *)np)->ann_pri_search(box_dist, store);  // search this subtree.
        else
            ((ANNkd_leaf *)np)->ann_pri_search(box_dist, store);
    }

    for (int i = 0; i < k; i++) {  // extract the k-th closest points
        dd[i] = store.ANNprPointMK->ith_smallest_key(i);
        nn_idx[i] = store.ANNprPointMK->ith_smallest_info(i);
    }

    delete store.ANNprPointMK; // deallocate closest point set
    delete store.ANNprBoxPQ;   // deallocate priority queue
}

//----------------------------------------------------------------------
//	kd_split::ann_pri_search - search a splitting node
//----------------------------------------------------------------------

void ANNkd_split::ann_pri_search(ANNdist box_dist, ANNprTempStore &store)
{
    ANNdist new_dist;    // distance to child visited later
    // distance to cutting plane
    ANNdist cut_diff = (ANNdist) store.ANNprQ[cut_dim] - (ANNdist) cut_val;

    if (cut_diff < 0) {  // left of cutting plane
        ANNdist box_diff = (ANNdist) cd_bnds[ANN_LO] - (ANNdist) store.ANNprQ[cut_dim];
        if (box_diff < 0)  // within bounds - ignore
            box_diff = 0;

        // distance to further box
        new_dist = (ANNdist) ANN_SUM(box_dist,
                                     ANN_DIFF(ANN_POW(box_diff), 
                                              ANN_POW(cut_diff)));

        if (child[ANN_HI] != KD_TRIVIAL)// enqueue if not trivial
            store.ANNprBoxPQ->insert(new_dist, child[ANN_HI]);

        // continue with closer child
        if (!child[ANN_LO]->isLeaf)
            ((ANNkd_split *)child[ANN_LO])->ann_pri_search(box_dist, store);
        else
            ((ANNkd_leaf *)child[ANN_LO])->ann_pri_search(box_dist, store);
    } else { // right of cutting plane
        ANNdist box_diff = 
            (ANNdist) store.ANNprQ[cut_dim] - (ANNdist) cd_bnds[ANN_HI];

        if (box_diff < 0)                               // within bounds - ignore
            box_diff = 0;

        // distance to further box
        new_dist = (ANNdist) ANN_SUM(box_dist,
                                     ANN_DIFF(ANN_POW(box_diff), 
                                              ANN_POW(cut_diff)));

        if (child[ANN_LO] != KD_TRIVIAL)// enqueue if not trivial
            store.ANNprBoxPQ->insert(new_dist, child[ANN_LO]);

        // continue with closer child
        if (!child[ANN_HI]->isLeaf)
            ((ANNkd_split *)child[ANN_HI])->ann_pri_search(box_dist, store);
        else
            ((ANNkd_leaf *)child[ANN_HI])->ann_pri_search(box_dist, store);
    }

    //ANN_SPL(1)                                                        // one more splitting node visited
    // ANN_FLOP(8)                                                      // increment floating ops
}

//----------------------------------------------------------------------
//	kd_leaf::ann_pri_search - search points in a leaf node
//
//		This is virtually identical to the ann_search for standard search.
//----------------------------------------------------------------------

void ANNkd_leaf::ann_pri_search(ANNdist box_dist, ANNprTempStore &store)
{
    register ANNdist min_dist;  // distance to k-th closest point

    // #define __ANN_L1_FAST__
#ifndef __ANN_L1_FAST__
    register ANNdist dist;      // distance to data point
    register ANNcoord* pp;      // data coordinate pointer
    register ANNcoord* qq;      // query coordinate pointer
    register ANNdist t;
    register int d, d2;
#else
    __m128i *pp2 = (__m128i *) store.ANNprQ;
#endif /* __ANN_L1_FAST__ */

    // KNS: note, hard-coding 128-dim features
    min_dist = store.ANNprPointMK->max_key(); // k-th smallest distance so far

#if 1
    for (int i = 0; i < n_pts; i++) {
        __builtin_prefetch(store.ANNprPts + (unsigned long long) bkt[i] * 128,
                           0 /* read */, 0 /* no locality */);
    }
#endif

    for (int i = 0; i < n_pts; i++) {   // check points in bucket
        // pp = store.ANNprPts + (unsigned long long) bkt[i] * store.ANNprDim;    // first coord of next data point
#ifndef __ANN_L1_FAST__
        pp = store.ANNprPts + (unsigned long long) bkt[i] * 128;    // first coord of next data point
        qq = store.ANNprQ; // first coord of query point
        dist = 0;

#if 0
        for(d = 0; d < 128; d++) {
            t = (ANNdist) *(qq++) - (ANNdist) *(pp++); // compute length and adv coordinate
            // exceeds dist to k-th smallest?
            if( (dist = ANN_SUM(dist, ANN_POW(t))) > min_dist) {
                break;
            }
        }

        if (d >= 128 && // among the k best?
            (ANN_ALLOW_SELF_MATCH || dist!=0)) { // and no self-match problem
            // add it to the list
            store.ANNprPointMK->insert(dist, bkt[i]);
            min_dist = store.ANNprPointMK->max_key();
        }
#else

        for (d = 0; d < 4; d++) {
            for (d2 = 0; d2 < 32; d2++) {
                t = (ANNdist) *(qq++) - (ANNdist) *(pp++);
                dist = ANN_SUM(dist, ANN_POW(t));
            }

            if( dist > min_dist) {
                break;
            }
        }

        if (d >= 4) { // && // among the k best?
            // (ANN_ALLOW_SELF_MATCH || dist!=0)) { // and no self-match problem
            // add it to the list
            store.ANNprPointMK->insert(dist, bkt[i]);
            min_dist = store.ANNprPointMK->max_key();
        }
#endif

#else        
        /* trying out intrinsics to compute L1 distance */
        __m128i *pp1 = 
            (__m128i *) (store.ANNprPts + (unsigned long long) bkt[i] * 128);
        /* compute all SADs */
        __m128i d1, d2, d3, d4, d5, d6, d7, d8;
        d1 = _mm_sad_epu8(pp1[0], pp2[0]);
        d2 = _mm_sad_epu8(pp1[1], pp2[1]);
        d3 = _mm_sad_epu8(pp1[2], pp2[2]);
        d4 = _mm_sad_epu8(pp1[3], pp2[3]);
        d5 = _mm_sad_epu8(pp1[4], pp2[4]);
        d6 = _mm_sad_epu8(pp1[5], pp2[5]);
        d7 = _mm_sad_epu8(pp1[6], pp2[6]);
        d8 = _mm_sad_epu8(pp1[7], pp2[7]);

        /* compute sums */
        __m128i s1, s2, s3, s4;
        s1 = _mm_add_epi64( d1, d2 );
        s2 = _mm_add_epi64( d3, d4 );
        s3 = _mm_add_epi64( d5, d6 );
        s4 = _mm_add_epi64( d7, d8 );
        s1 = _mm_add_epi64( s1, s2 );
        s3 = _mm_add_epi64( s3, s4 );
        s1 = _mm_add_epi64( s1, s3 );

        s2 = _mm_srli_si128 ( s1, 8 );
    
        int d = _mm_cvtsi128_si32( s1 ) + _mm_cvtsi128_si32( s2 );

        if (d <= min_dist) {
            store.ANNprPointMK->insert(d, bkt[i]);
            min_dist = store.ANNprPointMK->max_key();            
        }

        // ANNdist tt0 = ((ANNdist) qq[0]) - pp[0];
#endif
    }

    // ANN_LEAF(1)      // one more leaf node visited
    // ANN_PTS(n_pts)   // increment points visited
    store.ANNptsVisited += n_pts; // increment number of points visited
}