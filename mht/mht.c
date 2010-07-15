
/*********************************************************************
 * FILE: mht.C                                                       *
 *                                                                   *
 * AUTHOR: Matthew Miller (mlm)                                      *
 *                                                                   *
 * HISTORY:                                                          *
 *   12 AUG 93 -- (mlm) commented                                    *
 *                                                                   *
 * CONTENTS:                                                         *
 *                                                                   *
 *   Member functions for MHT objects.  See mht.H for details.       *
 *                                                                   *
 * ----------------------------------------------------------------- *
 *                                                                   *
 *             Copyright (c) 1993, NEC Research Institute            *
 *                       All Rights Reserved.                        *
 *                                                                   *
 *   Permission to use, copy, and modify this software and its       *
 *   documentation is hereby granted only under the following terms  *
 *   and conditions.  Both the above copyright notice and this       *
 *   permission notice must appear in all copies of the software,    *
 *   derivative works or modified versions, and any portions         *
 *   thereof, and both notices must appear in supporting             *
 *   documentation.                                                  *
 *                                                                   *
 *   Correspondence should be directed to NEC at:                    *
 *                                                                   *
 *                     Ingemar J. Cox                                *
 *                                                                   *
 *                     NEC Research Institute                        *
 *                     4 Independence Way                            *
 *                     Princeton                                     *
 *                     NJ 08540                                      *
 *                                                                   *
 *                     phone:  609 951 2722                          *
 *                     fax:  609 951 2482                            *
 *                     email:  ingemar@research.nj.nec.com (Inet)    *
 *                                                                   *
 *********************************************************************/

#define DECLARE_MHT

#include <stdio.h>

#include "mht.h"
#include "timer.h"

/*-------------------------------------------------------------------*
 | MHT::scan() -- do an iteration of the mht algorithm
 *-------------------------------------------------------------------*/

int MHT::scan()
{
  BGN

  Timer timer;

  G_numCallsToScan++;

  measureAndValidate();
  m_currentTime++;

  if( m_dbgStartA <= m_currentTime && m_currentTime < m_dbgEndA )
    doDbgA();

  m_activeTHypoList.removeAll();
  importNewReports();

  if( m_tTreeList.isEmpty() )
  {
    G_timeSpentInScan += timer.elapsedTime();
    return 0;
  }

  makeNewGroups();
  findGroupLabels();
  splitGroups();
  mergeGroups();

  if( m_dbgStartB <= m_currentTime && m_currentTime < m_dbgEndB )
    doDbgB();

  pruneAndHypothesize();
  removeUnusedTHypos();
  verifyTTreeRoots();

  removeUnusedTTrees();
  removeUnusedReports();
  removeUnusedGroups();

  updateActiveTHypoList();

  if( m_dbgStartC <= m_currentTime && m_currentTime < m_dbgEndC )
    doDbgC();

  G_timeSpentInScan += timer.elapsedTime();

  return 1;
}

/*-------------------------------------------------------------------*
 | MHT::importNewReports() -- set up to deal with new REPORTs
 |
 | This assigns a row number to each new REPORT, for use in making
 | assignment problems later on, and moves all the REPORTs from the
 | new report list to the old report list.
 *-------------------------------------------------------------------*/

void MHT::importNewReports()
{
  BGN

  PTR_INTO_iDLIST_OF< REPORT > reportPtr;
  int rowNum;

  rowNum = 0;
  LOOP_DLIST( reportPtr, m_newReportList )
  {
    (*reportPtr).setRowNum( rowNum++ );
  }

  m_oldReportList.splice( m_newReportList );
}

/*-------------------------------------------------------------------*
 | MHT::makeNewGroups() -- make a new GROUP for each new track tree
 *-------------------------------------------------------------------*/

void MHT::makeNewGroups()
{
  BGN

  for( ; m_nextNewTTree.isValid(); ++m_nextNewTTree )
  {
    m_groupList.append( new GROUP( m_nextNewTTree.get() ) );
  }
}

/*-------------------------------------------------------------------*
 | MHT::findGroupLabels() -- label track trees and REPORTs for
 |                           grouping
 |
 | This is the first step in splitting and merging GROUPs.  Here, we
 | find out which GROUP each T_TREE and REPORT should be in.  The
 | algorithm for doing this is derived from the one described in
 |
 |   T. Kurien
 |   Issues in the Design of Practical Multitarget Tracking
 |   Algorithms
 |   in Yaakov Bar-Shalom, Ed.
 |   Multitarget-Multisensor Tracking
 |
 | My (derived) algorithm proceeds in three steps:
 |
 | 1. All the T_TREE groupId members are initialized to -1, which   
 |    indicates they haven't been grouped yet.
 |
 | 2. We loop through the list of all the old REPORTs, assigning each
 |    a group id by means of the member function setAllGroupIds().
 |    This function also gives the group id to all the T_TREEs that
 |    refer to the REPORT.  If a T_TREE has already been given a
 |    group id by a previous REPORT, then that report is also given
 |    the new id.
 |
 | 3. After step 2, all the T_TREEs that refer to any REPORT have
 |    been given group id's.  But there might be a few T_TREEs that
 |    don't refer to any REPORTs at all.  The last step is to give
 |    each of these T_TREEs a unique group id (since each of them can
 |    be in a GROUP by itself).
 *-------------------------------------------------------------------*/

void MHT::findGroupLabels()
{
  BGN

  PTR_INTO_iDLIST_OF< T_TREE > tTreePtr;
  PTR_INTO_iDLIST_OF< REPORT > reportPtr;
  int groupId;

  LOOP_DLIST( tTreePtr, m_tTreeList )
  {
    (*tTreePtr).setGroupId( -1 );
  }

  groupId = 1;

  LOOP_DLIST( reportPtr, m_oldReportList )
  {
    (*reportPtr).setAllGroupIds( groupId++ );
  }

  LOOP_DLIST( tTreePtr, m_tTreeList )
  {
    if( (*tTreePtr).getGroupId() == -1 )
      (*tTreePtr).setGroupId( groupId++ );
  }

  #ifdef TSTBUG
    if( ! m_newReportList.isEmpty() )
      THROW_ERR( "m_newReportList must be empty in findGroupLabels()" )
  #endif

  #ifdef DEBUG
    LOOP_DLIST( reportPtr, m_oldReportList )
    {
      (*reportPtr).checkGroupIds();
    }
  #endif
}

/*-------------------------------------------------------------------*
 | MHT::splitGroups() -- split GROUPs that can split
 |
 | The hard part of this is handled in GROUP::splitIfYouMust().
 *-------------------------------------------------------------------*/

void MHT::splitGroups()
{
  BGN

  PTR_INTO_iDLIST_OF< GROUP > groupPtr;

  LOOP_DLIST( groupPtr, m_groupList )
  {
    (*groupPtr).splitIfYouMust();
  }
}

/*-------------------------------------------------------------------*
 | MHT::mergeGroups() -- merge GROUPs that must merge
 |
 | Two GROUPs must merge if they both contain T_TREEs with the same
 | group id.  This routine simply goes through the list of GROUPs
 | from head to tail, and, for each GROUP, searches the rest of the
 | list for a GROUP with the same group id.  When it finds one, it
 | merges the two GROUPs, removes the one it found, and continues
 | searching.
 *-------------------------------------------------------------------*/

void MHT::mergeGroups()
{
  BGN

  PTR_INTO_iDLIST_OF< GROUP > groupPtr0;
  PTR_INTO_iDLIST_OF< GROUP > groupPtr1;
  int groupId;

  LOOP_DLIST( groupPtr0, m_groupList )
  {
    groupId = (*groupPtr0).getGroupId();

    for( groupPtr1 = groupPtr0, ++groupPtr1;
         groupPtr1.isValid();
         ++groupPtr1 )
      if( (*groupPtr1).getGroupId() == groupId )
      {
        (*groupPtr0).merge( groupPtr1.get(),
                            m_logMinGHypoRatio,
                            m_maxGHypos );
        groupPtr1.remove();
      }
  }
}

/*-------------------------------------------------------------------*
 | MHT::pruneAndHypothesize() -- prune track trees and create new
 |                               G_HYPOs for each GROUP
 *-------------------------------------------------------------------*/

void MHT::pruneAndHypothesize()
{
  BGN

  PTR_INTO_iDLIST_OF< GROUP > groupPtr;

  LOOP_DLIST( groupPtr, m_groupList )
  {
    (*groupPtr).pruneAndHypothesize( m_maxDepth,
                                     m_logMinGHypoRatio,
                                     m_maxGHypos );
  }
}

void MHT::clear()
{
  BGN

  PTR_INTO_iDLIST_OF< GROUP > groupPtr;
  for (int i=m_maxDepth; i>=0; i--) 
  {
    LOOP_DLIST( groupPtr, m_groupList )
    {
      (*groupPtr).clear( i);
    }
    verifyTTreeRoots();
    removeUnusedTTrees();
    removeUnusedReports();
    removeUnusedGroups();
  }
  verifyLastTTreeRoots();
}

/*-------------------------------------------------------------------*
 | MHT::removeUnusedTHypos() -- remove the T_HYPOs that are not
 |                              referred to in any G_HYPO, or have
 |                              had all their children removed
 *-------------------------------------------------------------------*/

void MHT::removeUnusedTHypos()
{
  BGN

  PTR_INTO_iDLIST_OF< T_TREE > tTreePtr;
  PTR_INTO_iTREE_OF< T_HYPO > tHypoPtr;

  LOOP_DLIST( tTreePtr, m_tTreeList )
  {
    LOOP_TREEpostOrder( tHypoPtr, *(*tTreePtr).getTree() )
    {
      if( ! (*tHypoPtr).isInUse() )
        tHypoPtr.removeSubtree();
    }
  }
}

/*-------------------------------------------------------------------*
 | MHT::verifyTTreeRoots() -- verify and remove track tree roots that
 |                            have only one child
 *-------------------------------------------------------------------*/

void MHT::verifyTTreeRoots()
{
  BGN

  PTR_INTO_iDLIST_OF< T_TREE > tTreePtr;
  iTREE_OF< T_HYPO > *tTree;
  T_HYPO *root;

  LOOP_DLIST( tTreePtr, m_tTreeList )
  {
    tTree = (*tTreePtr).getTree();

    if( ! tTree->isEmpty() )
    {
      root = tTree->getRoot();
      while( root->hasOneChild() && ! root->endsTrack() )
      {
        if( root->mustVerify() )
          root->verify();
        tTree->removeRoot();

        root = tTree->getRoot();
      }

      if( root->endsTrack() && root->mustVerify() )
        root->verify();
    }
  }
}

void MHT::verifyLastTTreeRoots()
{
  BGN

  PTR_INTO_iDLIST_OF< T_TREE > tTreePtr;
  iTREE_OF< T_HYPO > *tTree;
  T_HYPO *root;

  LOOP_DLIST( tTreePtr, m_tTreeList )
  {
    tTree = (*tTreePtr).getTree();

    if( ! tTree->isEmpty() )
    {
      root = tTree->getRoot();
      if( root)
      {
        if( root->mustVerify() )
          root->verify();
        tTree->removeRoot();
      }

    }
  }
}

/*-------------------------------------------------------------------*
 | MHT::removeUnusedTTrees() -- remove track trees that aren't needed
 |
 | A track tree should be removed if either of the following is true:
 |
 | 1. It's root node ends the tree (since the root node has been
 |    identified as true, so the tree is done).
 |
 | 2. All possible paths from the root lead to nodes that end the
 |    tree, and contain no nodes that must be verified (basically,
 |    the tree isn't really done yet, but the application doesn't
 |    care what happens with it, so it can be discarded).
 *-------------------------------------------------------------------*/

void MHT::removeUnusedTTrees()
{
  BGN

  PTR_INTO_iDLIST_OF< T_TREE > tTreePtr;
  iTREE_OF< T_HYPO > *tTree;
  PTR_INTO_iTREE_OF< T_HYPO > tHypoPtr;
  int treeIsInUse;

  LOOP_DLIST( tTreePtr, m_tTreeList )
  {
    tTree = (*tTreePtr).getTree();
    treeIsInUse = 0;

    if( ! tTree->isEmpty() &&
        ! tTree->getRoot()->endsTrack() )
      LOOP_TREE( tHypoPtr, *tTree )
      {
        if( (*tHypoPtr).mustVerify() ||
            (tHypoPtr.isAtLeaf() && ! (*tHypoPtr).endsTrack()) )
        {
          treeIsInUse = 1;
          break;
        }
      }

    if( ! treeIsInUse )
      tTreePtr.remove();
  }
}

/*-------------------------------------------------------------------*
 | MHT::removeUnusedReports() -- remove REPORTs that aren't used by
 |                               any T_HYPOs
 *-------------------------------------------------------------------*/

void MHT::removeUnusedReports()
{
  BGN

  PTR_INTO_iDLIST_OF< REPORT > reportPtr;

  LOOP_DLIST( reportPtr, m_oldReportList )
    if( ! (*reportPtr).isInUse() )
      reportPtr.remove();
}

/*-------------------------------------------------------------------*
 | MHT::removeUnusedGroups() -- remove GROUPs that have no track
 |                              trees left in them
 *-------------------------------------------------------------------*/

void MHT::removeUnusedGroups()
{
  BGN

  PTR_INTO_iDLIST_OF< GROUP > groupPtr;

  LOOP_DLIST( groupPtr, m_groupList )
    if( ! (*groupPtr).isInUse() )
      groupPtr.remove();
}

/*-------------------------------------------------------------------*
 | MHT::updateActiveTHypoList() -- build the list of T_HYPOs that are
 |                                 leaves of track trees
 *-------------------------------------------------------------------*/

void MHT::updateActiveTHypoList()
{
  BGN

  PTR_INTO_iDLIST_OF< T_TREE > tTreePtr;
  PTR_INTO_iTREE_OF< T_HYPO > tHypoPtr;

  LOOP_DLIST( tTreePtr, m_tTreeList )
  {
    LOOP_TREE( tHypoPtr, *(*tTreePtr).getTree() )
    {
      if( tHypoPtr.isAtLeaf() )
        m_activeTHypoList.append( *tHypoPtr );
    }
  }
}

/*-------------------------------------------------------------------*
 | MHT::checkGroups() -- test that groups are correct (for debugging)
 *-------------------------------------------------------------------*/

void MHT::checkGroups()
{
  BGN

  PTR_INTO_iDLIST_OF< GROUP > groupPtr0;
  PTR_INTO_iDLIST_OF< GROUP > groupPtr1;
  int groupId;

  LOOP_DLIST( groupPtr0, m_groupList )
  {
    (*groupPtr0).check();
  }

  LOOP_DLIST( groupPtr0, m_groupList )
  {
    groupId = (*groupPtr0).getGroupId();

    for( (groupPtr1 = groupPtr0),++groupPtr1; groupPtr1.isValid(); ++groupPtr1)
      if( (*groupPtr1).getGroupId() == groupId )
        THROW_ERR( "Two groups with same id" )
  }
}

/*-------------------------------------------------------------------*
 | MHT::describe() -- verbose diagnostic
 *-------------------------------------------------------------------*/

void MHT::describe( int spaces )
{
  BGN

  PTR_INTO_ptrDLIST_OF< T_HYPO > tHypoPtr;
  PTR_INTO_iDLIST_OF< GROUP > groupPtr;
  PTR_INTO_iDLIST_OF< REPORT > reportPtr;
  PTR_INTO_iDLIST_OF< T_TREE > tTreePtr;
  int k;

  Indent( spaces ); cout << "MHT "; print(); cout << endl;
  spaces += 2;

  Indent( spaces );
  cout << "lastTrackUsed = " << m_lastTrackIdUsed;
  cout << ", time = " << m_currentTime;
  cout << endl;

  Indent( spaces );
  cout << "maxDepth = " << m_maxDepth;
  cout << ", logMinRatio = " << m_logMinGHypoRatio;
  cout << ", maxGHypos = " << m_maxGHypos;
  cout << endl;

  Indent( spaces ); cout << "active tHypo's:";
  k = 0;

  LOOP_DLIST( tHypoPtr, m_activeTHypoList )
  {
    if( k++ >= 3 )
    {
      cout << endl;
      Indent( spaces ); cout << "               ";
      k = 0;
    }

    cout << " "; (*tHypoPtr).print();
  }
  cout << endl;

  Indent( spaces ); cout << "===== clusters"; cout << endl;
  LOOP_DLIST( groupPtr, m_groupList )
  {
    (*groupPtr).describe( spaces + 2 );
  }

  Indent( spaces ); cout << "===== oldReports"; cout << endl;
  LOOP_DLIST( reportPtr, m_oldReportList )
  {
    (*reportPtr).describe( spaces + 2 );
  }

  Indent( spaces ); cout << "===== newReports"; cout << endl;
  LOOP_DLIST( reportPtr, m_newReportList )
  {
    (*reportPtr).describe( spaces + 2 );
  }

  Indent( spaces ); cout << "===== oldTrees"; cout << endl;
  LOOP_DLIST( tTreePtr, m_tTreeList )
  {
    if( tTreePtr == m_nextNewTTree )
    {
      Indent( spaces ); cout << "===== newTrees"; cout << endl;
    }

    cout << endl;
    (**(*tTreePtr).getTree()).describeTree( spaces + 2 );
  }
}

/*-------------------------------------------------------------------*
 | MHT::printStats() -- print out some information about the progress
 |                      of the mht
 *-------------------------------------------------------------------*/

void MHT::printStats( int spaces )
{
  BGN

  int totalTTrees = m_tTreeList.getLength();
  int totalTHypos = m_activeTHypoList.getLength();
  int totalGroups = m_groupList.getLength();
  int totalGHypos;
  int maxGHypos;
  int numGHypos;
  PTR_INTO_iDLIST_OF< GROUP > groupPtr;

  totalGHypos = 0;
  maxGHypos = 0;
  LOOP_DLIST( groupPtr, m_groupList )
  {
    numGHypos = (*groupPtr).getNumGHypos();

    totalGHypos += numGHypos;
    if( maxGHypos < numGHypos )
      maxGHypos = numGHypos;
  }

  Indent( spaces ); cout << "track trees ---------------- "
                         << totalTTrees << endl;
  Indent( spaces ); cout << "  track hypos:          "
                         << totalTHypos << endl;
  Indent( spaces ); cout << "  hypos per tree:       "
                         << (double)totalTHypos / totalTTrees << endl;
  Indent( spaces ); cout << "groups --------------------- "
                         << totalGroups << endl;
  Indent( spaces ); cout << "  group hypos:          "
                         << totalGHypos << endl;
  Indent( spaces ); cout << "  hypos per group:      "
                         << (double)totalGHypos / totalGroups << endl;
  Indent( spaces ); cout << "  max hypos in a group: "
                         << maxGHypos << endl;
}

/*-------------------------------------------------------------------*
 | Debugging routines
 *-------------------------------------------------------------------*/

void MHT::doDbgA()
{
  BGN

  cout << endl;
  cout << "  ************************** MHT after measureAndValidate()"
       << endl;

  describe( 4 );

  cout << "  HIT RETURN..." << endl;
  getchar();
}

void MHT::doDbgB()
{
  BGN

  cout << endl;
  cout << "  ******************************* MHT after group formation"
       << endl;

  describe( 4 );

  cout << "  HIT RETURN..." << endl;
  getchar();
}

void MHT::doDbgC()
{
  BGN

  cout << endl;
  cout << "  *************************************** MHT after pruning"
       << endl;

  describe( 4 );

  cout << "  HIT RETURN..." << endl;
  getchar();
}
