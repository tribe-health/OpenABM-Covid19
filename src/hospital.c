/*
 * hsopital.c
 *
 *  Created on: 30 Mar 2020
 *      Author: vuurenk
 */

#include "hospital.h"
#include "constant.h"
#include "utilities.h"
#include "network.h"
#include "model.h"
#include "disease.h"

/*****************************************************************************************
*  Name:		initialize_hospital
*  Description: initializes and individual at the start of the simulation, note can
*  				only be called once per individual
*  Returns:		void
******************************************************************************************/
void initialise_hospital(
    hospital *hospital,
    parameters *params,
    int hdx
)
{
    if( hospital->hospital_idx != 0 )
        print_exit( "a hospital can only be initialised once!");

    hospital->hospital_idx   = hdx;

    hospital->n_total_beds = params->hospital_n_beds;
    hospital->n_total_icus = params->hospital_n_icus;

    hospital->general_patient_pdxs = calloc( hospital->n_total_beds, sizeof(long) ); //TODO: should memory allocated be size of beds + icus??
    hospital->icu_patient_pdxs     = calloc( hospital->n_total_icus, sizeof(long) );

    //setup wards
    hospital->n_wards = calloc( N_HOSPITAL_WARD_TYPES, sizeof(int*) );
    hospital->wards = calloc( N_HOSPITAL_WARD_TYPES, sizeof(ward*) );

    for( int w_type = 0; w_type < N_HOSPITAL_WARD_TYPES; w_type++ )
    {
        hospital->wards[w_type] = calloc( hospital->n_wards[w_type], sizeof(ward) );
        for( int n_ward = 0; n_ward < hospital->n_wards[w_type]; n_ward++ )
            initialise_ward( &(hospital->wards[w_type][n_ward]), w_type, n_ward);
    }
}

/*****************************************************************************************
*  Name:		set_up_hospital_networks
*  Description: calls setup functions for all networks related to the hospital instance
*  Returns:		void
******************************************************************************************/
void set_up_hospital_networks( hospital* hospital )
{
    int idx, n_healthcare_workers;
    int ward_idx, ward_type;
    long *healthcare_workers;

    //setup hospital workplace network
    n_healthcare_workers = 0;
    healthcare_workers = calloc( hospital->n_total_doctors + hospital->n_total_nurses, sizeof(long) );

    //setup hcw -> patient networks for all wards
    for ( ward_type = 0; ward_type < N_HOSPITAL_WARD_TYPES; ward_type++ )
    {
        for( ward_idx = 0; ward_idx < hospital->n_wards[N_HOSPITAL_WARD_TYPES]; ward_idx++ )
        {
            for( idx = 0; idx < hospital->wards[ward_type][ward_idx].n_doctors; idx++ )
                healthcare_workers[n_healthcare_workers++] = hospital->wards[ward_type][ward_idx].doctors[idx].pdx;

            for( idx = 0; idx < hospital->wards[ward_type][ward_idx].n_nurses; idx++ )
                healthcare_workers[n_healthcare_workers++] = hospital->wards[ward_type][ward_idx].nurses[idx].pdx;

            set_up_ward_networks( &(hospital->wards[ward_type][ward_idx]) );
        }
    }

    //setup hcw workplace network
    hospital->hospital_workplace_network = calloc( 1, sizeof( network ));
    hospital->hospital_workplace_network = new_network( n_healthcare_workers, HOSPITAL_WORK );
    int n_interactions = 20;//TODO: maybe make this number of interactions set via param... and should nurses have more??
    build_watts_strogatz_network( hospital->hospital_workplace_network, n_healthcare_workers, n_interactions, 0.1, TRUE ); //TODO: p_rewire probability higher??
    relabel_network( hospital->hospital_workplace_network, healthcare_workers );

    free( healthcare_workers );
}

void build_hospital_networks( model *model, hospital *hospital )
{
    int ward_type, ward_idx;

    for( ward_type = 0; ward_type < N_HOSPITAL_WARD_TYPES; ward_type++ )
        for( ward_idx = 0; ward_idx < hospital->n_wards[ward_type]; ward_idx++ )
            build_ward_networks( model, &(hospital->wards[ward_type][ward_idx]) );
}

/*****************************************************************************************
*  Name:		add_nurse
*  Description: adds population id of a doctor / nurse to hospital's
*               doctor / nurse population id list
******************************************************************************************/
void add_healthcare_worker_to_hospital(hospital *hospital, long pdx, int type)
{
    int ward_type, ward_idx;
    int ward_found = FALSE;

    if( type == DOCTOR )
    {
        for( ward_type = 0; ward_type < N_HOSPITAL_WARD_TYPES && ward_found != TRUE; ward_type++ )
            for( ward_idx = 0; ward_idx < hospital->n_wards[ward_type] && ward_found != TRUE; ward_idx++ )
                if( hospital->wards[ward_type][ward_idx].n_doctors < hospital->wards[ward_type][ward_idx].n_max_doctors )
                    ward_found = TRUE;

        if( ward_found == FALSE)
            print_exit( "attempted to allocated more than max number of doctors to hospital!!" );

        hospital->n_total_doctors++;
        initialise_doctor( &(hospital->wards[ward_type][ward_idx].doctors[hospital->wards[ward_type][ward_idx].n_doctors++]) , pdx, hospital->hospital_idx, ward_idx, ward_type);
    }
    else if( type == NURSE )
    {
        for( ward_type = 0; ward_type < N_HOSPITAL_WARD_TYPES && ward_found != TRUE; ward_type++ )
            for( ward_idx = 0; ward_idx < hospital->n_wards[ward_type] && ward_found != TRUE; ward_idx++ )
                if( hospital->wards[ward_type][ward_idx].n_nurses < hospital->wards[ward_type][ward_idx].n_max_nurses )
                    ward_found = TRUE;

        if( ward_found == FALSE)
            print_exit( "attempted to allocated more than max number of nurses to hospital!!" );

        hospital->n_total_nurses++;
        initialise_nurse( &(hospital->wards[ward_type][ward_idx].nurses[hospital->wards[ward_type][ward_idx].n_nurses++]) , pdx, hospital->hospital_idx, ward_idx, ward_type);
    }
}

/*****************************************************************************************
*  Name:		add_nurse
*  Description: adds population id of a doctor / nurse to hospital's
*               doctor / nurse population id list
******************************************************************************************/
void add_patient_to_hospital(hospital *hospital, long pdx, int type)
{
    if( type == HOSPITALISED )
    {
        hospital->general_patient_pdxs[hospital->n_total_general_patients++] = pdx;
        hospital->n_total_general_patients++;
    }
    else if( type == HOSPITALISED_CRITICAL)
    {
        hospital->icu_patient_pdxs[hospital->n_total_icu_patients++] = pdx;
        hospital->n_total_icu_patients++;
    }
}


int healthcare_worker_working( individual* indiv )
{
    if( indiv->status == DEATH || is_in_hospital(indiv) || indiv->quarantined == TRUE )
        return FALSE;

    return TRUE;
}

/*****************************************************************************************
*  Name:		destroy_hospital
*  Description: Destroys the model structure and releases its memory
******************************************************************************************/
void destroy_hospital( hospital *hospital)
{
    free( hospital->hospital_workplace_network );
}

/*****************************************************************************************
*  Name:		transition_one_hospital_event
*  Description: Generic function for updating an individual with their next
*				event and adding the applicable events
*  Returns:		void
******************************************************************************************/
void transition_one_hospital_event(
        model *model,
        individual *indiv,
        int from,
        int to,
        int edge
)
{
    indiv->status           = from;

    if( from != NO_EVENT )
        indiv->time_event[from] = model->time;
    if( indiv->current_hospital_event != NULL )
        remove_event_from_event_list( model, indiv->current_hospital_event );
    if( indiv->next_hospital_event != NULL )
        indiv->current_hospital_event = indiv->next_hospital_event;

    if( to != NO_EVENT )
    {
        indiv->time_event[to]     = model->time + ifelse( edge == NO_EDGE, 0,
                sample_transition_time( model, edge ) );  // TOM: PROBABLY NEEDS SOME PARAMETERISATION HERE?
        indiv->next_hospital_event = add_individual_to_event_list( model, to, indiv, indiv->time_event[to] );
    }
}

/*****************************************************************************************
*  Name:		transition_to_waiting
*  Description: Transitions a severely symptomatic individual from the general populace
*               to the admissions list for the hospital.
*               At the moment - severely symptomatic refers to HOSPITALISED individuals.
*  Returns:		void
******************************************************************************************/
void transition_to_waiting( model *model, individual *indiv )
{
    // set_hospital(model->params, indiv); Search for hospital with space.
    set_waiting( indiv, model->params, 1);
}
/*****************************************************************************************
*  Name:		transition_to_general
*  Description: Transitions a severely symptomatic individual from the admissions list to a
*               general ward.
*               This only occurs if there is enough space in the general wards.
*  Returns:		void
******************************************************************************************/
void transition_to_general( model *model, individual *indiv )
{
    //TODO: CHECK FOR BED AVAILABILITY HERE.

    //if (indiv->hospital_location == WAITING)
    //      if( assign_to_general_ward( indiv->general_ward, indiv->hospital ) == TRUE ); // Search for an empty general ward,
    //                                                                          adds to the general ward network and then updates the patient ward number.
    //          set_general_admission( indiv, model->params, 1); // Changes hospital location and adjusts daily connections.

    //else (indiv->hospital_location == ICU)
    //  if (assign_to_general_ward( indiv->hospital )); -> Indiv -> Check if previous ward has space, if not (or indiv has no prior general ward), assign to a new one, returns true.
    //  -> if this fails, then return false. Add to general ward network if successful.
    //      set_general_admission( indiv, model->params, 1);    // Changes hospital location and adjusts daily connections.


    set_general_admission( indiv, model->params, 1);
}
/*****************************************************************************************
*  Name:		transition_to_ICU
*  Description: Transitions a critically ill individual to the ICU from either a general
*               ward or the admissions list. This only occurs if there is enough space in
*               the ICU.
*  Returns:		void
******************************************************************************************/
void transition_to_icu( model *model, individual *indiv )
{
    //TODO: CHECK FOR BED AVAILABILITY HERE.

    //if (indiv->hospital_location == WAITING)
    //  if (set_hospital(indiv)); -> Search for a hospital with space. Returns true if space is found while setting the hospital.
    //      assign_to_icu_ward( indiv->hospital ); -> Indiv has no ward, then check for empty ward, then assign.
    //      add_to_icu_ward( indiv->general_ward_number, indiv->hospital );
    //      set_icu_admission( indiv, model->params, 1);

    //else (indiv->hospital_location == ICU)
    //  assign_to_icu_ward( indiv->hospital ); -> Indiv -> Check if previous ward has space, if not (or indiv has no prior ward), assign to a new one.
    //  add_to_icu_ward( indiv->general_ward_number, indiv->hospital );
    //  set_general_admission( indiv, model->params, 1);

    set_icu_admission( indiv, model->params, 1);
}
/*****************************************************************************************
*  Name:		transition_to_mortuary
*  Description: Transitions a newly deceased person from either the general ward or ICU
*               into a "morutary" to free space for new patients.
*  Returns:		void
******************************************************************************************/
void transition_to_mortuary( model *model, individual *indiv )
{
    set_mortuary_admission( indiv, model->params, 1);
    transition_one_hospital_event( model, indiv, MORTUARY, NO_EVENT, NO_EDGE );
}
/*****************************************************************************************
*  Name:		transition_to_populace
*  Description: Transitions a recovered individual from either the general wards or ICU
*               back into the general population.
*  Returns:		void
******************************************************************************************/
void transition_to_populace( model *model, individual *indiv )
{
    set_discharged( indiv, model->params, 1);
    transition_one_hospital_event( model, indiv, DISCHARGED, NO_EVENT, NO_EDGE );
}

// void set_hospital(model->params, indiv); Search for hospital with space in a general ward, then assign the hospital to that individual.
//                                          Consider what happens when there's no space
//                                          at any hospital. Output that it's OVERSUBSCRIBED.
// int assign_to_general_ward( indiv, hospital ); Search for an empty general ward in a particular hospital,
//                                                                      adds to the general ward patient list.
//                                                                      Then return TRUE if the patient has been reassigned.
//                                                                      Otherwise, return FALSE.
// int assign_to_icu_ward( indiv, hospital ); Search for an empty ICU ward in a particular hospital,
//                                                                      adds to the general ward patient list.
//                                                                      Then return TRUE if the patient has been reassigned.
//                                                                      Otherwise, return FALSE.
// void release_from_hospital ( indiv, ward ); Remove them from ward patient list and update ward totals.
