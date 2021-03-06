/*
The contents of this file are subject to the NOKOS License Version 1.0
(the "License"); you may not use this file except in compliance with the
License.

Software distributed under the License is distributed on an "AS IS" basis,
WITHOUT WARRANTY OF  ANY KIND, either express or implied. See the License
for the specific language governing rights and limitations under the License.

The Original Software is
TVT-tools.

Copyright © 2001 Nokia and others. All Rights Reserved.

Contributor(s): Juha Nieminen.
*/

// FILE_DES: OutputLSTS.cc: ag2lsts
// Nieminen Juha

static const char * const ModuleVersion=
  "Module version: $Id: OutputLSTS.cc 1.24 Fri, 27 Feb 2004 16:36:58 +0200 warp $";
// 
// ag2lsts:n tulos-lsts-luokan toteutus
//

// $Log:$

#include "OutputLSTS.hh"
#include "LSTS_File/oLSTS_File.hh"

using namespace std;

// Konstruktori ja destruktori
//===========================================================================
OutputLSTS::OutputLSTS(InputLSTS& ilsts):
    a_ilsts(ilsts),
    a_transitions(ilsts.getHeaderData().GiveActionCnt(),
                  ilsts.getHeaderData().GiveStateCnt(), true)
{
    a_states.resize(a_ilsts.getHeaderData().GiveStateCnt()+1);
    a_initialState = ilsts.getStateNumber(a_ilsts.getInitialState());
}

OutputLSTS::~OutputLSTS()
{
}

// Uuden tilan lisäys
// ------------------
inline OutputLSTS::StateHandle
OutputLSTS::addNewState(StatePropsContainer::StatePropsPtr stateprops)
{
    a_states.push_back(State(stateprops));
    return a_states.size()-1;
}

// Tsekkaa, onko tilassa vain yksi hyväksymisjoukko, ja vastaako sen aktiot
// tilasta lähteviä transitioita
// ------------------------------------------------------------------------
inline bool OutputLSTS::transitionsEqualAccsets(const InputLSTS::State& state)
{
    const RO_AccSets accsets = a_ilsts.getRO_AccSets(state);

    // Jos hyväksymisjoukkoja ei ole tasan yksi tai divbitti on päällä,
    // voimme välittömästi palata:
    if(accsets.setsAmount() != 1 || state.getDivBit()) return false;

    // Käydään läpi kaikki transitiot ja hyväksymisjoukon aktiot ja
    // katsotaan vastaako ne toisiaan:
    InputLSTS::tr_const_iterator triter = a_ilsts.tr_begin(state);
    RO_AccSets::AccSetPtr accsetiter = accsets.firstAccSet();
    RO_AccSets::action_const_iterator action = accsets.begin(accsetiter);
    while(triter != a_ilsts.tr_end(state) &&
          action != accsets.end(accsetiter))
    {
        // Jos transitio ei vastaa hyväksymisjoukon aktiota, on testi epätosi:
        lsts_index_t trAction = triter->getTransitionNumber();
        if(trAction != *action)
        {
            return false;
        }

        // Samannumeroiset transitiot skipataan (koska syöte voi olla
        // epädeterministinen):
        do
        {
            ++triter;
        } while(triter->getTransitionNumber() == trAction);

        ++action;
    }
    return triter == a_ilsts.tr_end(state) &&
        action == accsets.end(accsetiter);
}

// Lisää kaikki syötetilasta lähtevät transitiot tuloksen vastaavaan tilaan.
// Kutsutaan silloin, kun kyseisellä tilalla on vain yksi hyväksymisjoukko,
// joka sisältää samat aktiot kuin lähtevät transitiot.
// ------------------------------------------------------------------------
inline void OutputLSTS::addAllTransitions(const InputLSTS::State& state)
{
    for(InputLSTS::tr_const_iterator tr = a_ilsts.tr_begin(state);
        tr != a_ilsts.tr_end(state); ++tr)
    {
        a_transitions.addTransitionToState
            (tr->getTransitionNumber(),
             a_ilsts.getStateNumber(tr->getDestinationState()));
    }
}

// ag->lsts -muunnoksen pääalgoritmi
//===========================================================================
void OutputLSTS::calculateLSTS()
{
    // Käydään kaikki tilat läpi ja suoritetaan jokaiselle sama algoritmi
    // (jokaiselle itsenäisesti; muut tilat eivät vaikuta käsiteltävään
    // tilaan)
    for(lsts_index_t stateNumber = 1;
        stateNumber <= a_ilsts.getHeaderData().GiveStateCnt(); stateNumber++)
    {
        const InputLSTS::State& state = a_ilsts.getState(stateNumber);
        const RO_AccSets accsets = a_ilsts.getRO_AccSets(stateNumber);
        a_states[stateNumber].setStatePropsPtr(state.getStatePropsPtr());

        /*
        check_claim(!(accsets.setsAmount()==0 && !state.getDivBit()),
                    valueToMessage("Internal error: State ", stateNumber,
                                   " has empty accsets and divbit off. "
                                   "(Input is most probably not an AG)"));
        */

        // Hyväksymisjoukkojen muunto.
        // ---------------------------
        // Jos on yksi hyväkysmisjoukko jossa samat aktiot kuin tilasta
        // lähtevät transitiot:
        if(transitionsEqualAccsets(state))
        {
            // niin lisätään ne sellaisenaan tulokseen:
            a_transitions.startAddingTransitionsToState(stateNumber);
            addAllTransitions(state);
        }
        else
        {
            // muuten luodaan uusi tila jokaista hyväksymisjukkoa kohden:
            unsigned stateIndexBegin = a_states.size();
            for(RO_AccSets::AccSetPtr accset = accsets.firstAccSet();
                accset != accsets.endAccSet(); ++accset)
            {
                StateHandle newState = addNewState(state.getStatePropsPtr());
                a_transitions.startAddingTransitionsToState(newState);

                // ja lisätään siihen kyseisen hyväksymisjoukon aktiot:
                for(InputLSTS::tr_const_iterator tr = a_ilsts.tr_begin(state);
                    tr != a_ilsts.tr_end(state); ++tr)
                {
                    if(tr->getTransitionNumber() != 0 &&
                       accsets.isMember(tr->getTransitionNumber(),accset))
                    {
                        unsigned dest =
                            a_ilsts.getStateNumber(tr->getDestinationState());
                        a_transitions.addTransitionToState
                            (tr->getTransitionNumber(), dest);
                    }
                }

                a_transitions.doneAddingTransitionsToState();
            }

            // Lopuksi lisätään kaaret itse käsiteltävään tilaan:
            a_transitions.startAddingTransitionsToState(stateNumber);

            // Ensin tau-kaaret uusin tiloihin:
            for(; stateIndexBegin < a_states.size(); stateIndexBegin++)
            {
                a_transitions.addTransitionToState(0, stateIndexBegin);
            }
            // Sitten ne kaaret, jotka eivät esiinny hyväksymisjoukoissa:
            BitVector actions = accsets.getAllUsedActionsAsBitVector();
            for(InputLSTS::tr_const_iterator tr = a_ilsts.tr_begin(state);
                tr != a_ilsts.tr_end(state); ++tr)
            {
                unsigned action = tr->getTransitionNumber();
                if(action==0 || !actions[action-1])
                {
                    unsigned dest =
                        a_ilsts.getStateNumber(tr->getDestinationState());
                    a_transitions.addTransitionToState(action, dest);
                }
            }
        } // else

        // Muutetaan div-bitti tau-kaareksi
        if(state.getDivBit())
        {
            a_transitions.addTransitionToState(0, stateNumber);
        }

        a_transitions.doneAddingTransitionsToState();
    } // for

    // Jos alkutila ei ole vakaa tehdään uusi alkutila, josta tau-kaari
    // entiseen.
    if(a_ilsts.getHeaderData().isInitiallyUnstable() &&
       !a_ilsts.getInitialState().getDivBit() &&
       (a_transitions.numberOfTransitions(a_initialState) &&
        a_transitions.getTransition(a_initialState, 0).transitionNumber != 0))
    {
        StateHandle newState =
            addNewState(a_ilsts.getInitialState().getStatePropsPtr());
        a_transitions.startAddingTransitionsToState(newState);
        a_transitions.addTransitionToState(0, a_initialState);
        a_transitions.doneAddingTransitionsToState();
        a_initialState = newState;
    }
}

// Kirjoitusmetodit
//-------------------------------------------------------------------------
void OutputLSTS::FillSPContainer(StatePropsContainer& spcont)
{
    const StatePropsContainer& ispcont = a_ilsts.getStateProps();

    // State prop names:
    for(unsigned i=1; i<=ispcont.getMaxStatePropNameNumber(); ++i)
        spcont.getStatePropName(i) = ispcont.getStatePropName(i);

    // State props:
    for(unsigned state = 1; state < a_states.size(); state++)
    {
        spcont.assignStateProps(state, a_states[state].getStatePropsPtr());
    }
}

void OutputLSTS::writeLSTSFile(OutStream& os)
{
    oLSTS_File ofile;

    ofile.AddTransitionsWriter(a_transitions);
    ofile.AddActionNamesWriter(a_ilsts.getActionNamesStore());

    StatePropsContainer spcont(a_ilsts.getStateProps().getMaxVal());
    FillSPContainer(spcont);
    ofile.AddStatePropsWriter(spcont);

    Header headerData = a_ilsts.getHeaderData();
    headerData.SetStateCnt(a_states.size()-1);
    headerData.SetInitialState(a_initialState);
    headerData.SetNoTaus(false);
    headerData.SetDeterministic(false);
    headerData.RemoveInitiallyUnstable();

    ofile.GiveHeader() = headerData;

    ofile.WriteFile(os);
}
