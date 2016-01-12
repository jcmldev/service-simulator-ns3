/*
 * Simulator of Service-based systems hosted in networks with dynamic topology i.e. MANETs, VANETs, HWNs, etc
 *
 * Author: Petr Novotny, Imperial College London
 * Release date: 10/12/2012
 *
 * Based on NS-3 version: 3.13
 * To run and modify, go to the "main" method at the end of the file
 */

#include <fstream>
#include <iostream>
#include <map>
#include <stdio.h>
#include <sys/time.h>
#include <climits>

#include "ns3/core-module.h"
//#include "ns3/common-module.h"
//#include "ns3/simulator-module.h"
//#include "ns3/node-module.h"
//#include "ns3/helper-module.h"
#include "ns3/mobility-module.h"
//#include "ns3/contrib-module.h"
#include "ns3/wifi-module.h"
#include "ns3/config.h"
#include "ns3/olsr-helper.h"
//#include "src/routing/list-routing/helper/ipv4-list-routing-helper.h"
//#include "src/routing/static-routing/helper/ipv4-static-routing-helper.h"

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
//#include "ns3/point-to-point-module.h"


#include "ns3/olsr-routing-protocol.h"
//src/olsr/model/olsr-routing-protocol.h

using namespace ns3;
using namespace std;
using namespace ns3::Config;
using namespace ns3::olsr;





class InstanceCounter
{
private:
	const char*				 				m_name;

	static map<const char*, uint32_t>		s_instanceCounters;


protected:

	InstanceCounter (const char* name)
	:m_name(name)
	{
		map<const char*, uint32_t>::iterator it;

		it=s_instanceCounters.find(m_name);

		if (it==s_instanceCounters.end())
		{
			s_instanceCounters[m_name] = 0;
		}

		s_instanceCounters[m_name]++;
	}

	virtual ~InstanceCounter ()
	{
		s_instanceCounters[m_name]--;
	}

public:

	static void WriteOut()
	{
		map<const char*, uint32_t>::iterator it;

		NS_LOG_UNCOND("	Instance counters at time: " << Simulator::Now());

		for ( it=s_instanceCounters.begin() ; it != s_instanceCounters.end(); it++ )
		{
			NS_LOG_UNCOND("		" << (*it).second << " " << (*it).first);
		}
	}

}; // InstanceCounter

map<const char*, uint32_t> InstanceCounter::s_instanceCounters;







/****************************************************************************************
 * Configuration model
 *
 * defines following:
 * - ExecutionStep						- definition of dependency and processing from client/service to contract method
 * - ExecutionPlan						- definition of set of steps and additional pre/post execution delays
 * - Client								- definition of client configuration
 * - ServiceMethod						- definition of implementation of method from contract by service
 * - Service							- definition of service configuration
 * - ServiceConfiguration				- definition of services, clients etc
 */

class FaultModel : public Object, public InstanceCounter
{
protected:
	bool				m_isEnabled;
	bool 				m_isGeneratingException;

public:

	FaultModel (bool isEnabled, bool isGeneratingException)
	:InstanceCounter(typeid(this).name()),
	 m_isEnabled(isEnabled),
	 m_isGeneratingException(isGeneratingException)
	{ }

	virtual ~FaultModel () {}

	virtual bool IsCorrupt (bool & isGeneratingException)
	{
		if (!IsEnabled ())
		{
			return false;
		}
		return IsCorruptQuery(isGeneratingException);
	}

	virtual Ptr<FaultModel> Clone () const = 0;
	void Enable (void) { m_isEnabled = true; }
	void Disable (void) { m_isEnabled = false; }
	bool IsEnabled (void) const { return m_isEnabled; }

private:
	virtual bool IsCorruptQuery (bool & isGeneratingException) = 0;

}; // FaultModel


class CompositeFaultModel : public FaultModel
{
private:
	list<Ptr<FaultModel> >		m_faultModels;

public:
	CompositeFaultModel(bool isEnabled)
	:FaultModel(isEnabled, false)
	{}

	virtual ~CompositeFaultModel () {}

	void AddFaultModel(Ptr<FaultModel> faultModel)
	{
		Ptr<FaultModel> faultModelCopy;


		faultModelCopy = faultModel->Clone();

		m_faultModels.push_back(faultModelCopy);
	}

	virtual Ptr<FaultModel> Clone () const
	{
		Ptr<CompositeFaultModel> 						clone = CreateObject<CompositeFaultModel>(m_isEnabled);
		list<Ptr<FaultModel> >							faultModelsCopy;
		list<Ptr<FaultModel> >::const_iterator			fmit;
		Ptr<FaultModel>									faultModel;
		Ptr<FaultModel>									faultModelCopy;


		for (fmit = m_faultModels.begin(); fmit != m_faultModels.end(); fmit++)
		{
			faultModel = *fmit;
			faultModelCopy = faultModel->Clone();
			faultModelsCopy.push_back(faultModelCopy);
		}

		clone->m_faultModels = faultModelsCopy;

		return clone;
	}

protected:

	/*
	 *
	 * How it works
	 *
	 * Fault models are ordered in the list according to their assumed priority
	 * 	based on order of insertion
	 *
	 * The method - Queries ordered list one by one, first positive result to fail will end the iteration
	 * 	- isGenerationgException will use the value from first positive result
	 *  - queries only enabled
	 *
	 * */
	virtual bool IsCorruptQuery (bool & isGeneratingException)
	{
		list<Ptr<FaultModel> >::const_iterator			fmit;
		Ptr<FaultModel>									faultModel;
		bool											isCorrupt = false;


		for (fmit = m_faultModels.begin(); fmit != m_faultModels.end(); fmit++)
		{
			faultModel = *fmit;

			if (faultModel->IsEnabled())
			{
				isCorrupt = faultModel->IsCorrupt(isGeneratingException);

				if (isCorrupt)
				{
					return true;
				}
			}
		}

		isGeneratingException = false;

		return false;
	}

}; // CompositeFaultModel


class SingleRateFaultModel : public FaultModel
{
private:
	double 				m_rate;
	RandomVariable 		m_ranvar;

public:

	SingleRateFaultModel (bool isEnabled, bool isGeneratingException, double rate, RandomVariable ranvar)
	:FaultModel(
			isEnabled,
			isGeneratingException),
	 m_rate(rate),
	 m_ranvar(ranvar)
	{ }

	virtual ~SingleRateFaultModel () {}

	virtual Ptr<FaultModel> Clone () const
	{
		return CreateObject<SingleRateFaultModel>(m_isEnabled, m_isGeneratingException, m_rate, m_ranvar);
	}

private:

	virtual bool IsCorruptQuery (bool & isGeneratingException)
	{
		isGeneratingException = m_isGeneratingException;
		return (m_ranvar.GetValue () < m_rate);
	}

}; // SingleRateFaultModel


class AbsoluteTimeFaultModel : public FaultModel
{
private:
	Time				m_from;
	Time				m_to;

public:

	AbsoluteTimeFaultModel (bool isEnabled, bool isGeneratingException, Time from, Time to)
	:FaultModel(
			isEnabled,
			isGeneratingException),
	 m_from(from),
	 m_to(to)
	{ }

	virtual ~AbsoluteTimeFaultModel () {}

	virtual Ptr<FaultModel> Clone () const
	{
		return CreateObject<AbsoluteTimeFaultModel>(
				m_isEnabled,
				m_isGeneratingException,
				m_from,
				m_to);
	}

private:

	virtual bool IsCorruptQuery (bool & isGeneratingException)
	{
		isGeneratingException = m_isGeneratingException;
		return ((m_from >= Simulator::Now()) && (m_to <= Simulator::Now()));
	}

}; // AbsoluteTimeFaultModel


class OnOffTimeFaultModel : public FaultModel
{
private:
	bool 				m_state;
	RandomVariable 		m_offRanvar;
	RandomVariable 		m_onRanvar;
	EventId				m_switchingEvent;

public:

	OnOffTimeFaultModel (
			bool isEnabled,
			bool isGeneratingException,
			bool state,
			RandomVariable offRanvar,
			RandomVariable onRanvar)
	:FaultModel(
			isEnabled,
			isGeneratingException),
	 m_state(state),
	 m_offRanvar(offRanvar),
	 m_onRanvar(onRanvar)
	{
		m_state = !m_state;
		ChangeState();
	}

	virtual ~OnOffTimeFaultModel ()
	{
		m_switchingEvent.Cancel();
	}

	Ptr<FaultModel> Clone () const
	{
		return CreateObject<OnOffTimeFaultModel>(
				m_isEnabled,
				m_isGeneratingException,
				m_state,
				m_offRanvar,
				m_onRanvar);
	}

private:

	virtual bool IsCorruptQuery (bool & isGeneratingException)
	{
		isGeneratingException = m_isGeneratingException;
		//NS_LOG_UNCOND( "IsCorruptQuery state: " << m_state);
		return m_state;
	}

	void ChangeState ()
	{
		Time nextSwhitchTimePeriod;


		m_state = !m_state;

		if (m_state)
		{
			nextSwhitchTimePeriod = MilliSeconds(m_onRanvar.GetInteger());
		}
		else
		{
			nextSwhitchTimePeriod = MilliSeconds(m_offRanvar.GetInteger());
		}

		m_switchingEvent = Simulator::Schedule (
				nextSwhitchTimePeriod,
				&OnOffTimeFaultModel::ChangeState,
				this);

		//NS_LOG_UNCOND( "state: " << m_state << " next switch: " << nextSwhitchTimePeriod);
	}

}; // OnOffTimeFaultModel


class OnOffRateFaultModel : public FaultModel
{
private:
	bool 				m_state;
	double 				m_offRate;
	RandomVariable 		m_offRanvar;
	double 				m_onRate;
	RandomVariable 		m_onRanvar;

public:

	OnOffRateFaultModel (
			bool isEnabled,
			bool isGeneratingException,
			bool state,
			double offRate,
			RandomVariable offRanvar,
			double onRate,
			RandomVariable onRanvar)
	:FaultModel(
			isEnabled,
			isGeneratingException),
	 m_state(state),
	 m_offRate(offRate),
	 m_offRanvar(offRanvar),
	 m_onRate(onRate),
	 m_onRanvar(onRanvar)
	{ }

	virtual ~OnOffRateFaultModel () {}

	Ptr<FaultModel> Clone () const
	{
		return CreateObject<OnOffRateFaultModel>(
				m_isEnabled,
				m_isGeneratingException,
				m_state,
				m_offRate,
				m_offRanvar,
				m_onRate,
				m_onRanvar);
	}

private:

	virtual bool IsCorruptQuery (bool & isGeneratingException)
	{
		bool changeState;


		if (m_state)
		{
			changeState = (m_onRanvar.GetValue () < m_onRate);
		}
		else
		{
			changeState = (m_offRanvar.GetValue () < m_offRate);
		}

		m_state = changeState ? !m_state : m_state;

		isGeneratingException = m_isGeneratingException;
		return m_state;
	}

}; // OnOffRateFaultModel





class ExecutionStep : public Object
{
private:
	const uint32_t 				m_contractId;
	const uint32_t 				m_contractMethodId;
	const RandomVariable 		m_requestSize;
	const double				m_stepProbability;

public:

	ExecutionStep(
			uint32_t contractId,
			uint32_t contractMethodId,
			RandomVariable requestSize,
			double stepProbability)
		:m_contractId (contractId),
		 m_contractMethodId (contractMethodId),
		 m_requestSize (requestSize),
		 m_stepProbability (stepProbability)
	{
		NS_ASSERT(contractId != 0);
		NS_ASSERT(contractMethodId != 0);
		NS_ASSERT(stepProbability != 0);
	}

	virtual ~ExecutionStep() {}

	uint32_t GetContractId () const { return m_contractId; }
	uint32_t GetContractMethodId () const { return m_contractMethodId; }
	const RandomVariable GetRequestSize () const { return m_requestSize; }
	const double GetStepProbability () const { return m_stepProbability; }

}; // ExecutionStep


class ExecutionPlan : public Object
{
private:
	vector<Ptr<ExecutionStep> > 		m_executionSteps;

public:

	ExecutionPlan ()
	{}

	virtual ~ExecutionPlan() {}

	const vector<Ptr<ExecutionStep> > & GetExecutionSteps () const { return m_executionSteps; }
	const Ptr<ExecutionStep> GetExecutionStep (uint32_t index) const { return m_executionSteps[index]; }
	const uint32_t GetExecutionStepsCount () const { return m_executionSteps.size(); }

	virtual void AddExecutionStep (
			uint32_t contractId,
			uint32_t contractMethodId,
			RandomVariable requestSize,
			double stepProbability)
	{
		Ptr<ExecutionStep> executionStep = CreateObject<ExecutionStep> (
				contractId,
				contractMethodId,
				requestSize,
				stepProbability);

		m_executionSteps.push_back( executionStep );
	}

}; // ExecutionPlan



class ServiceExecutionPlan : public ExecutionPlan
{
private:
	const RandomVariable 				m_planPreExeDelay;
	const RandomVariable 				m_planPostExeDelay;
	const RandomVariable 				m_stepPostExeDelay;
	const RandomVariable 				m_postPlanErrorDelay;

public:

	ServiceExecutionPlan (
			RandomVariable planPreExeDelay,
			RandomVariable planPostExeDelay,
			RandomVariable stepPostExeDelay,
			RandomVariable postPlanErrorDelay)
		:m_planPreExeDelay (planPreExeDelay),
		 m_planPostExeDelay (planPostExeDelay),
		 m_stepPostExeDelay (stepPostExeDelay),
		 m_postPlanErrorDelay (postPlanErrorDelay)
	{}

	virtual ~ServiceExecutionPlan() {}

	const RandomVariable GetPlanPreExeDelay() const { return m_planPreExeDelay; }
	const RandomVariable GetPlanPostExeDelay() const { return m_planPostExeDelay; }
	const RandomVariable GetStepPostExeDelay() const { return m_stepPostExeDelay; }
	const RandomVariable GetPostPlanErrorDelay() const { return m_postPlanErrorDelay; }

}; // ServiceExecutionPlan



class ClientExecutionPlan : public ExecutionPlan
{
private:
	const RandomVariable 				m_requestRate;
	const RandomVariable				m_afterFailureWaitingPeriod;

public:

	ClientExecutionPlan (
			RandomVariable requestRate,
			RandomVariable afterFailureWaitingPeriod)
		:m_requestRate (requestRate),
		 m_afterFailureWaitingPeriod (afterFailureWaitingPeriod)
	{}

	virtual ~ClientExecutionPlan() {}

	const RandomVariable GetRequestRate() const { return m_requestRate; }
	const RandomVariable GetAfterFailureWaitingPeriod() const { return m_afterFailureWaitingPeriod; }

}; // ClientExecutionPlan



class ServiceBase : public Object, public InstanceCounter
{
private:
	const uint32_t 						m_serviceId;
	const Time							m_startTime;
	const Time							m_stopTime;
	const Time 							m_responseTimeout;
	const Time 							m_ACKTimeout;
	const uint32_t						m_retransmissionLimit;
	const Time 							m_msgIdLifetime;


public:

	ServiceBase (
			uint32_t serviceId,
			Time startTime,
			Time stopTime,
			Time responseTimeout,
			Time ACKTimeout,
			uint32_t retransmissionLimit,
			Time msgIdLifetime)
		:InstanceCounter(typeid(this).name()),
		 m_serviceId (serviceId),
		 m_startTime(startTime),
		 m_stopTime(stopTime),
		 m_responseTimeout(responseTimeout),
		 m_ACKTimeout(ACKTimeout),
		 m_retransmissionLimit(retransmissionLimit),
		 m_msgIdLifetime(msgIdLifetime)
	{
		NS_ASSERT(serviceId != 0);
		NS_ASSERT(startTime.GetMilliSeconds() != 0);
		NS_ASSERT(stopTime.GetMilliSeconds() != 0);
		NS_ASSERT(responseTimeout.GetMilliSeconds() != 0);
		NS_ASSERT(ACKTimeout.GetMilliSeconds() != 0);
		NS_ASSERT(retransmissionLimit != 0);
		NS_ASSERT(msgIdLifetime.GetMilliSeconds() != 0);
	}

	virtual ~ServiceBase() {}

	uint32_t GetServiceId () const { return m_serviceId; }
	Time GetStartTime () const { return m_startTime; }
	Time GetStopTime () const { return m_stopTime; }
	Time GetResponseTimeout () const { return m_responseTimeout; }
	Time GetACKTimeout () const { return m_ACKTimeout; }
	uint32_t GetRetransmissionLimit () const { return m_retransmissionLimit; }
	Time GetMsgIdLifetime () const { return m_msgIdLifetime; }

}; // ServiceBase


class Client : public ServiceBase
{
private:
	const Ptr<ExecutionPlan>		m_executionPlan;

public:

	Client (uint32_t serviceId,
			Time startTime,
			Time stopTime,
			Time responseTimeout,
			Time ACKTimeout,
			uint32_t retransmissionLimit,
			Time msgIdLifetime,
			Ptr<ExecutionPlan> executionPlan)
		:ServiceBase(
				serviceId,
				startTime,
				stopTime,
				responseTimeout,
				ACKTimeout,
				retransmissionLimit,
				msgIdLifetime),
		 m_executionPlan(executionPlan)
	{
		NS_ASSERT(executionPlan != NULL);
	}

	virtual ~Client() {}

	Ptr<ExecutionPlan> GetExecutionPlan () const { return m_executionPlan; }

}; // Client


class Service;

class ServiceMethod : public Object
{
private:
	const uint32_t							m_contractMethodId;
	const Ptr<Service>						m_service;
	const RandomVariable					m_responseSize;
	Ptr<FaultModel> 						m_faultModel;
	const Ptr<ServiceExecutionPlan>			m_executionPlan;

public:

	ServiceMethod (
			uint32_t contractMethodId,
			Ptr<Service> service,
			RandomVariable responseSize,
			Ptr<FaultModel> faultModel,
			Ptr<ServiceExecutionPlan> executionPlan)
		:m_contractMethodId(contractMethodId),
		 m_service (service),
		 m_responseSize (responseSize),
		 m_faultModel (faultModel->Clone()),
		m_executionPlan(executionPlan)
	{
		NS_ASSERT(contractMethodId != 0);
		NS_ASSERT(service != NULL);
		NS_ASSERT(faultModel != NULL);
		NS_ASSERT(executionPlan != NULL);
	}

	virtual ~ServiceMethod() {}

	Ptr<ServiceMethod> CreateReplica(
			Ptr<Service> newService)
	{
		return CreateObject<ServiceMethod>(
			m_contractMethodId,
			newService,
			m_responseSize,
			m_faultModel,
			m_executionPlan); // plan remains the same - same configuraiton of steps and dependencies
	}

	const uint32_t GetContractMethodId () const { return m_contractMethodId; }
	const Ptr<Service> GetService () const { return m_service; }
	const RandomVariable GetResponseSize () const { return m_responseSize; }
	const Ptr<FaultModel> GetFaultModel () const { return m_faultModel; }
	const Ptr<ServiceExecutionPlan> GetExecutionPlan () const { return m_executionPlan; }

	void SetFaultModel (Ptr<FaultModel> faultModel)
	{
		NS_ASSERT(faultModel != NULL);
		m_faultModel = faultModel->Clone();
	}


}; // ServiceMethod


class Service : public ServiceBase
{
private:
	const uint32_t 								m_contractId;
	Ptr<FaultModel> 							m_faultModel;
	map<uint32_t, Ptr<ServiceMethod> >			m_methods;
	const RandomVariable						m_postErrorDelay;

public:

	Service (uint32_t serviceId,
			Time startTime,
			Time stopTime,
			Time responseTimeout,
			Time ACKTimeout,
			uint32_t retransmissionLimit,
			Time msgIdLifetime,
			uint32_t contractId,
			Ptr<FaultModel> faultModel,
			RandomVariable postErrorDelay)
		:ServiceBase(
				serviceId,
				startTime,
				stopTime,
				responseTimeout,
				ACKTimeout,
				retransmissionLimit,
				msgIdLifetime),
		m_contractId (contractId),
		m_faultModel (faultModel->Clone()),
		m_postErrorDelay (postErrorDelay)
	{
		NS_ASSERT(contractId != 0);
		NS_ASSERT(faultModel != NULL);
	}

	virtual ~Service() {}

	Ptr<Service> CreateReplica(uint32_t newServiceId)
	{
		Ptr<Service>											service;
		map<uint32_t, Ptr<ServiceMethod> >::const_iterator		it;
		Ptr<ServiceMethod>										method;


		service = new Service (
				newServiceId,
				GetStartTime(),
				GetStopTime(),
				GetResponseTimeout(),
				GetACKTimeout(),
				GetRetransmissionLimit(),
				GetMsgIdLifetime(),
				m_contractId,
				m_faultModel,
				m_postErrorDelay);


		for (it = m_methods.begin(); it != m_methods.end(); it++)
		{
			method = it->second;
			method = method->CreateReplica(service);

			service->m_methods.insert(
					pair<uint32_t, Ptr<ServiceMethod> > (method->GetContractMethodId(), method) );
		}

		return service;
	}


	uint32_t GetContractId () const { return m_contractId; }
	const Ptr<FaultModel> GetFaultModel () const { return m_faultModel; }
	const map<uint32_t, Ptr<ServiceMethod> > GetMethods () const { return m_methods; }
	const Ptr<ServiceMethod> GetMethod (uint32_t methodContractId) const { return m_methods.find(methodContractId)->second; }
	const RandomVariable GetPostErrorDelay () const { return m_postErrorDelay; }

	Ptr<ServiceMethod> AddMethod (
		uint32_t contractMethodId,
		RandomVariable responseSize,
		Ptr<FaultModel> faultModel,
		Ptr<ServiceExecutionPlan> executionPlan)
	{
		NS_ASSERT(contractMethodId != 0);
		NS_ASSERT(faultModel != NULL);
		NS_ASSERT(executionPlan != NULL);

		Ptr<ServiceMethod> 	method = CreateObject<ServiceMethod>(
				contractMethodId,
				this,
				responseSize,
				faultModel,
				executionPlan);

		m_methods.insert( pair<uint32_t, Ptr<ServiceMethod> > (contractMethodId, method) );

		return method;
	}

	void SetFaultModel (Ptr<FaultModel> faultModel)
	{
		NS_ASSERT(faultModel != NULL);
		m_faultModel = faultModel->Clone();
	}

}; // Service






struct Arc
{
	Arc(uint32_t h, uint32_t t)
	{
		head = h;
		tail = t;
	}

	uint32_t head;
	uint32_t tail;
};

bool operator < (const Arc& a, const Arc& b)
{
	if (a.head < b.head) return true;
	if (a.tail < b.tail) return true;
	return false;
}




class ServiceConfiguration : public Object
{
private:
	map<uint32_t, Ptr<Service> >			m_services;
	map<uint32_t, Ptr<Service> >			m_contracts;
	map<uint32_t, Ptr<Client> >				m_clients;
	bool 									m_deployClientsRandomly;

public:

	ServiceConfiguration ()
	{
		m_deployClientsRandomly=true;
	}

	virtual ~ServiceConfiguration() {}

	Ptr<Service> GetService (uint32_t serviceId) const { return m_services.find(serviceId)->second; }
	Ptr<Service> GetContract (uint32_t contractId) const { return m_contracts.find(contractId)->second; }
	Ptr<Client> GetClient (uint32_t clientId) const { return m_clients.find(clientId)->second; }
	const map<uint32_t, Ptr<Service> > & GetServices () const { return m_services; }
	const map<uint32_t, Ptr<Client> > & GetClients () const { return m_clients; }
	const bool GetDeployClientsRandomly () const { return m_deployClientsRandomly; }
	void SetDeployClientsRandomly (bool value) { m_deployClientsRandomly=value; }

	void AddService(
			uint32_t serviceId,
			Time startTime,
			Time stopTime,
			Time responseTimeout,
			Time ACKTimeout,
			uint32_t retransmissionLimit,
			Time msgIdLifetime,
			uint32_t contractId,
			Ptr<FaultModel> faultModel,
			RandomVariable postErrorDelay)
	{
		NS_ASSERT(serviceId != 0);
		NS_ASSERT(contractId != 0);
		NS_ASSERT(faultModel != NULL);

		Ptr<Service> 			service;


		service = new Service (
				serviceId,
				startTime,
				stopTime,
				responseTimeout,
				ACKTimeout,
				retransmissionLimit,
				msgIdLifetime,
				contractId,
				faultModel,
				postErrorDelay);

		m_services.insert( pair<uint32_t, Ptr<Service> > (serviceId, service));
		m_contracts.insert( pair<uint32_t, Ptr<Service> > (contractId, service));
	}

	void AddServiceReplica(
			uint32_t serviceId,
			uint32_t newServiceId)
	{
		NS_ASSERT(serviceId != 0);
		NS_ASSERT(newServiceId != 0);

		Ptr<Service> 			service;
		Ptr<Service> 			newService;


		service = GetService(serviceId);
		newService = service->CreateReplica(newServiceId);

		m_services.insert( pair<uint32_t, Ptr<Service> > (newServiceId, newService));
		m_contracts.insert( pair<uint32_t, Ptr<Service> > (newService->GetContractId(), newService));
	}

	Ptr<ServiceMethod> AddServiceMethod (
			uint32_t serviceId,
			uint32_t contractMethodId,
			RandomVariable responseSize,
			Ptr<FaultModel> faultModel,
			RandomVariable planPreExeDelay,
			RandomVariable planPostExeDelay,
			RandomVariable stepPostExeDelay,
			RandomVariable postPlanErrorDelay)
	{
		NS_ASSERT(serviceId != 0);
		NS_ASSERT(contractMethodId != 0);
		NS_ASSERT(faultModel != NULL);

		Ptr<Service> service = GetService(serviceId);
		Ptr<ServiceExecutionPlan> plan = CreateObject<ServiceExecutionPlan>(
				planPreExeDelay,
				planPostExeDelay,
				stepPostExeDelay,
				postPlanErrorDelay);


		NS_ASSERT(service != NULL);

		return service->AddMethod(
				contractMethodId,
				responseSize,
				faultModel,
				plan);
	}

	void AddServiceExecutionStep (
			uint32_t serviceId,
			uint32_t contractMethodId,
			uint32_t destContractId,
			uint32_t destContractMethodId,
			RandomVariable requestSize,
			double stepProbability)
	{
		NS_ASSERT(serviceId != 0);
		NS_ASSERT(contractMethodId != 0);
		NS_ASSERT(destContractId != 0);
		NS_ASSERT(destContractMethodId != 0);
		NS_ASSERT(stepProbability != 0);

		Ptr<Service> service = GetService(serviceId);
		Ptr<ServiceMethod> method = service->GetMethod(contractMethodId);
		Ptr<ExecutionPlan> plan = method->GetExecutionPlan();


		NS_ASSERT(service != NULL);
		NS_ASSERT(method != NULL);
		NS_ASSERT(plan != NULL);

		plan->AddExecutionStep(
				destContractId,
				destContractMethodId,
				requestSize,
				stepProbability);
	}

	void AddClient(
			uint32_t clientId,
			Time startTime,
			Time stopTime,
			Time responseTimeout,
			Time ACKTimeout,
			uint32_t retransmissionLimit,
			Time msgIdLifetime,
			RandomVariable planRequestRate,
			RandomVariable afterFailureWaitingPeriod)
	{
		NS_ASSERT(clientId != 0);

		Ptr<ExecutionPlan> 	plan = CreateObject<ClientExecutionPlan>(
				planRequestRate,
				afterFailureWaitingPeriod);

		Ptr<Client> 				client = new Client (
				clientId,
				startTime,
				stopTime,
				responseTimeout,
				ACKTimeout,
				retransmissionLimit,
				msgIdLifetime,
				plan);


		m_clients.insert( pair<uint32_t, Ptr<Client> > (clientId, client));
	}

	void AddClientExecutionStep (
			uint32_t clientId,
			uint32_t destContractId,
			uint32_t destContractMethodId,
			RandomVariable requestSize,
			double stepProbability)
	{
		NS_ASSERT(clientId != 0);
		NS_ASSERT(destContractId != 0);
		NS_ASSERT(destContractMethodId != 0);

		Ptr<Client> 			client = GetClient(clientId);
		Ptr<ExecutionPlan>		plan = client->GetExecutionPlan();

		//NS_LOG_UNCOND(clientId << ' ' << destMethodId << ' ' << stepProbability);
		plan->AddExecutionStep(
				destContractId,
				destContractMethodId,
				requestSize,
				stepProbability);
	}

	/* Checks ServiceConfiguration for following inconsistencies
	 * Service
	 * - at least one service in ServiceConfiguration
	 * - each service has at least one method
	 * - at least one client in ServiceConfiguration
	 * - each client has at least one execution step
	 * - each execution step points to existing contract and method
	 */
	bool CheckServiceConfiguration ()
	{
		bool 	bPass = true;


		NS_LOG_UNCOND("Service configuration check started ...");

		bPass = CheckClients() ? bPass : false;
		bPass = CheckServices() ? bPass : false;

		NS_LOG_UNCOND("Service configuration check finished with result: " << (bPass ? "Passed" : "failed"));

		return bPass;
	}

	void WriteOutStatistics () const
	{
		int 		numberOfClientExecutionSteps = 0;
		int			numberOfServiceMethods = 0;
		int			numberOfServiceExecutionSteps = 0;
		int			numberOfOrphanServices = 0;

		map<uint32_t, Ptr<Service> >::const_iterator		sit;
		Ptr<Service>										service;
		map<uint32_t, Ptr<ServiceMethod> >					serviceMethods;
		map<uint32_t, Ptr<ServiceMethod> >::const_iterator 	smit;
		Ptr<ServiceMethod>									serviceMethod;
		map<uint32_t, Ptr<Service> >						contractsCopy(m_contracts);
		map<uint32_t, Ptr<Client> >::const_iterator			cit;
		Ptr<Client>											client;
		Ptr<ExecutionPlan>									plan;


		// services - number of execution steps
		for (sit = m_services.begin(); sit != m_services.end(); sit++)
		{
			service = sit->second;
			numberOfServiceMethods += service->GetMethods().size();
			serviceMethods = service->GetMethods();

			// service methods
			for (smit = serviceMethods.begin(); smit != serviceMethods.end(); smit++)
			{
				serviceMethod = smit->second;
				plan = serviceMethod->GetExecutionPlan();
				numberOfServiceExecutionSteps += plan->GetExecutionStepsCount();
				RemoveExecutionPlanContractsFromVector(plan, contractsCopy);
			}
		}

		// clients - number of execution steps
		for (cit = m_clients.begin(); cit != m_clients.end(); cit++)
		{
			client = cit->second;
			plan = client->GetExecutionPlan();
			numberOfClientExecutionSteps += plan->GetExecutionStepsCount();
			RemoveExecutionPlanContractsFromVector(plan, contractsCopy);
		}

		numberOfOrphanServices = contractsCopy.size();

		NS_LOG_UNCOND("Service configuration statistics ...");
		NS_LOG_UNCOND("	Number of clients: " << m_clients.size());
		NS_LOG_UNCOND("	Number of clients' execution steps: " << numberOfClientExecutionSteps);
		NS_LOG_UNCOND("	Number of services: " << m_services.size());
		NS_LOG_UNCOND("	Number of services' methods: " << numberOfServiceMethods);
		NS_LOG_UNCOND("	Number of services' execution steps: " << numberOfServiceExecutionSteps);
		NS_LOG_UNCOND("	Number of orphan services: " << numberOfOrphanServices);
	}

	void WriteOutGraphProperties ()
	{
		set<Arc> arcs;
		map<uint32_t, Ptr<Service> >::const_iterator		sit;
		map<uint32_t, Ptr<Client> >::const_iterator			cit;
		Ptr<Service> service;
		Ptr<Client> client;
		map<uint32_t, Ptr<ServiceMethod> >					serviceMethods;
		map<uint32_t, Ptr<ServiceMethod> >::const_iterator 	smit;
		Ptr<ServiceMethod>									serviceMethod;


		// load clients arcs
		for (cit = m_clients.begin(); cit != m_clients.end(); cit++)
		{
			client = cit->second;

			LoadArcsFromPlan(
					arcs,
					client->GetServiceId(),
					client->GetExecutionPlan());
		}

		// load services arcs
		for (sit = m_services.begin(); sit != m_services.end(); sit++)
		{
			service = sit->second;
			serviceMethods = service->GetMethods();

			// service methods
			for (smit = serviceMethods.begin(); smit != serviceMethods.end(); smit++)
			{
				serviceMethod = smit->second;

				LoadArcsFromPlan(
						arcs,
						service->GetServiceId(),
						serviceMethod->GetExecutionPlan());
			}
		}


		// node degrees
		map<uint32_t, uint32_t> serviceIndegree;
		map<uint32_t, uint32_t> serviceOutdegree;
		map<uint32_t, uint32_t> clientOutdegree;
		Arc arc(0, 0);
		set<Arc>::const_iterator		ait;


		// load contracts and services
		for (sit = m_services.begin(); sit != m_services.end(); sit++)
		{
			service = sit->second;
			serviceIndegree.insert(pair<uint32_t, uint32_t>(service->GetContractId(), 0));
			serviceOutdegree.insert(pair<uint32_t, uint32_t>(service->GetServiceId(), 0));
		}

		// load clients
		for (cit = m_clients.begin(); cit != m_clients.end(); cit++)
		{
			client = cit->second;
			clientOutdegree.insert(pair<uint32_t, uint32_t>(client->GetServiceId(), 0));
		}

		// unique set of: client/service(head) and contract(tail)
		for (ait = arcs.begin(); ait != arcs.end(); ait++)
		{
			arc = *ait;

			// services (contracts) indegree - includes arcs from clients
			// for each contract(tail) occurrence add 1
			if (serviceIndegree.find(arc.tail) != serviceIndegree.end())
			{
				serviceIndegree.find(arc.tail)->second++;
			}

			// services outdegree
			// for each service(head) occurrence add 1
			if (serviceOutdegree.find(arc.head) != serviceOutdegree.end())
			{
				serviceOutdegree.find(arc.head)->second++;
			}

			// clients outdegree
			// for each client (head) occurrence add 1
			if (clientOutdegree.find(arc.head) != clientOutdegree.end())
			{
				clientOutdegree.find(arc.head)->second++;
			}
		}

		NS_LOG_UNCOND("Service graph properties - DAG ...");
		NS_LOG_UNCOND("	Services indegree - calculated on service nodes only, including edges from client nodes");
		WriteOutGraphVectorStats(serviceIndegree);
		NS_LOG_UNCOND("	Services outdegree - calculated on service nodes only");
		WriteOutGraphVectorStats(serviceOutdegree);
		NS_LOG_UNCOND("	Clients outdegree - calculated on client nodes only");
		WriteOutGraphVectorStats(clientOutdegree);
	}

private:

	void LoadArcsFromPlan (
			set<Arc> &arcs,
			uint32_t head,
			const Ptr<ExecutionPlan> 	plan) const
	{
		NS_ASSERT(plan != NULL);

		vector<Ptr<ExecutionStep> >::const_iterator			esit;
		Ptr<ExecutionStep>									step;
		uint32_t											tail;


		for (esit = plan->GetExecutionSteps().begin(); esit != plan->GetExecutionSteps().end(); esit++)
		{
			step = *esit;
			tail = step->GetContractId();
			arcs.insert(Arc(head, tail));
		}
	}

	void WriteOutGraphVectorStats(map<uint32_t, uint32_t> graphVector)
	{
		map<uint32_t, uint32_t>::const_iterator dit;
		uint32_t	min = 10000;
		uint32_t	max = 0;
		uint32_t	sum = 0;
		double		avg = 0;
		uint32_t	size = 0;
		uint32_t	degree = 0;


		for (dit = graphVector.begin(); dit != graphVector.end(); dit++)
		{
			degree = dit->second;
			// min
			if (min > degree) min = degree;
			// max
			if (max < degree) max = degree;
			// sum
			sum += degree;
		}

		// size
		size = graphVector.size();
		// avg
		avg = ((double)sum) / ((double)size);

		NS_LOG_UNCOND("		Min: " << min);
		NS_LOG_UNCOND("		Max: " << max);
		NS_LOG_UNCOND("		Sum: " << sum);
		NS_LOG_UNCOND("		Mean: " << avg);
		NS_LOG_UNCOND("		Size (number of nodes): " << size);
	}

	void RemoveExecutionPlanContractsFromVector (
			const Ptr<ExecutionPlan> plan,
			map<uint32_t, Ptr<Service> > & contractsCopy) const
	{
		NS_ASSERT(plan != NULL);

		vector<Ptr<ExecutionStep> >::const_iterator			esit;
		Ptr<ExecutionStep>									step;


		for (esit = plan->GetExecutionSteps().begin(); esit != plan->GetExecutionSteps().end(); esit++)
		{
			step = *esit;

			if (contractsCopy.find(step->GetContractId()) != contractsCopy.end())
			{
				contractsCopy.erase(
					contractsCopy.find(step->GetContractId()));
			}
		}
	}

	bool CheckClients ()
	{
		map<uint32_t, Ptr<Client> >::iterator		it;
		Ptr<Client>									client;
		Ptr<ExecutionPlan>							plan;


		// at least one client in ServiceConfiguration
		if (m_clients.size() == 0)
		{
			NS_LOG_UNCOND("	error: there are non clients");
			return false;
		}
		else
		{
			NS_LOG_UNCOND("	number of clients: " << m_clients.size());
		}

		// each client has at least one execution step
		for (it = m_clients.begin(); it != m_clients.end(); it++)
		{
			client = it->second;
			plan = client->GetExecutionPlan();
			if (plan->GetExecutionStepsCount() == 0)
			{
				NS_LOG_UNCOND("	error: following client has no execution steps: " << client->GetServiceId());
				return false;
			}
			else
			{
				if (!CheckExecutionPlan(plan))
				{
					NS_LOG_UNCOND("	client: " << client->GetServiceId());
					return false;
				}
			}
		}

		NS_LOG_UNCOND("	each client has at least one execution step");
		NS_LOG_UNCOND("	each clients' execution step points to existing contract and method");

		return true;
	}

	bool CheckServices ()
	{
		map<uint32_t, Ptr<Service> >::iterator		it;
		Ptr<Service>								service;


		// at least one service in ServiceConfiguration
		if (m_services.size() == 0)
		{
			NS_LOG_UNCOND("	error: there are non services");
			return false;
		}
		else
		{
			NS_LOG_UNCOND("	number of services: " << m_services.size());
		}

		// each service has at least one method
		for (it = m_services.begin(); it != m_services.end(); it++)
		{
			service = it->second;
			if (service->GetMethods().size() == 0)
			{
				NS_LOG_UNCOND("	error: following service has no methods: " << service->GetServiceId());
				return false;
			}
			else
			{
				if (!CheckServiceExecutionPlans(service))
				{
					return false;
				}
			}
		}

		NS_LOG_UNCOND("	each service has at least one method");
		NS_LOG_UNCOND("	each services' execution step points to existing contract and method");

		return true;
	}

	bool CheckServiceExecutionPlans (Ptr<Service> service)
	{
		NS_ASSERT(service != NULL);

		const map<uint32_t, Ptr<ServiceMethod> > & 			serviceMethods = service->GetMethods();
		map<uint32_t, Ptr<ServiceMethod> >::const_iterator 	x;
		Ptr<ServiceMethod>									serviceMethod;


		for (x = serviceMethods.begin(); x != serviceMethods.end(); x++)
		{
			serviceMethod = x->second;

			if (!CheckExecutionPlan(serviceMethod->GetExecutionPlan()))
			{
				NS_LOG_UNCOND("	service: " << service->GetServiceId());
				NS_LOG_UNCOND("	method: " << serviceMethod->GetContractMethodId());
				return false;
			}
		}

		return true;
	}
	bool CheckExecutionPlan (const Ptr<ExecutionPlan> plan)
	{
		NS_ASSERT(plan != NULL);

		vector<Ptr<ExecutionStep> >::const_iterator		it;
		Ptr<ExecutionStep>								step;
		Ptr<Service>									contract;


		for (it = plan->GetExecutionSteps().begin(); it != plan->GetExecutionSteps().end(); it++)
		{
			step = *it;
			contract = GetContract(step->GetContractId());

			if (contract == 0)
			{
				NS_LOG_UNCOND("	error: execution step to non existing contract id: " << step->GetContractId());
				return false;
			}

			if (contract->GetMethod(step->GetContractMethodId()) == 0)
			{
				NS_LOG_UNCOND("	error: execution step to non existing method id: " << step->GetContractMethodId());
				return false;
			}
		}

		return true;
	}

}; // ServiceConfiguration











/****************************************************************************************
 * Messaging layer
 *
 * Wraps network layer and separates it from service layer
 *
 * defines following:
 * - Message
 * - SimulationOutput
 * - MessageEndpoint
 * - ClientMessageEndpoint
 * - ServerMessageEndpoint
 * - UdpClientMessageEndpoint
 * - UdpServerMessageEndpoint
 * - MessageEndpointFactory
 *
 * */


class Message : public Header, public Object, public InstanceCounter
{
private:
	static uint32_t s_messageCounter;
	static uint32_t s_conversationCounter;

	uint32_t m_messageType;
	uint32_t m_messageId;
	uint32_t m_relatedToMessageId;
	uint32_t m_conversationId;
	uint32_t m_srcNode;
	uint32_t m_srcService;
	uint32_t m_destNode;
	uint32_t m_destService;
	uint32_t m_destMethod;
	uint32_t m_size;

public:

	enum MessageType
	{
		MTRequest = 1,
		MTResponse = 2,
		MTResponseException = 3,
		MTACK = 4
	};

#define ACK_MESSAGE_SIZE 100
#define RESPONSE_EXCEPTION_MESSAGE_SIZE 100



	Message ()
		:InstanceCounter(typeid(this).name()),
		m_messageType (MTRequest),
		m_messageId (0),
		m_relatedToMessageId (0),
		m_conversationId (0),
		m_srcNode (0),
		m_srcService (0),
		m_destNode (0),
		m_destService (0),
		m_destMethod (0),
		m_size (0)
	{

	}

	virtual ~Message () {}

	void InitializeNew(
			uint32_t srcNode,
			uint32_t srcService,
			uint32_t destNode,
			uint32_t destService,
			uint32_t destMethod,
			uint32_t size)
	{
		m_messageType = MTRequest;
		m_messageId = ++s_messageCounter;
		m_relatedToMessageId = 0;
		m_conversationId = ++s_conversationCounter;
		m_srcNode = srcNode;
		m_srcService = srcService;
		m_destNode = destNode;
		m_destService = destService;
		m_destMethod = destMethod;
		m_size = size;
	}

	void InitializeResponse (Ptr<Message> sourceMsg, uint32_t size)
	{
		NS_ASSERT(sourceMsg != NULL);

		m_messageType = MTResponse;
		m_messageId = ++s_messageCounter;
		m_relatedToMessageId = sourceMsg->m_messageId;
		m_conversationId = sourceMsg->m_conversationId;
		m_srcNode = sourceMsg->m_srcNode;
		m_srcService = sourceMsg->m_srcService;
		m_destNode = sourceMsg->m_destNode;
		m_destService = sourceMsg->m_destService;
		m_destMethod = sourceMsg->m_destMethod;
		m_size = size;
	}

	void InitializeACK (Ptr<Message> sourceMsg)
	{
		NS_ASSERT(sourceMsg != NULL);

		m_messageType = MTACK;
		m_messageId = ++s_messageCounter;
		m_relatedToMessageId = sourceMsg->m_messageId;
		m_conversationId = sourceMsg->m_conversationId;
		m_srcNode = sourceMsg->m_srcNode;
		m_srcService = sourceMsg->m_srcService;
		m_destNode = sourceMsg->m_destNode;
		m_destService = sourceMsg->m_destService;
		m_destMethod = sourceMsg->m_destMethod;
		m_size = ACK_MESSAGE_SIZE;
	}

	void InitializeResponseException (Ptr<Message> sourceMsg)
	{
		NS_ASSERT(sourceMsg != NULL);

		m_messageType = MTResponseException;
		m_messageId = ++s_messageCounter;
		m_relatedToMessageId = sourceMsg->m_messageId;
		m_conversationId = sourceMsg->m_conversationId;
		m_srcNode = sourceMsg->m_srcNode;
		m_srcService = sourceMsg->m_srcService;
		m_destNode = sourceMsg->m_destNode;
		m_destService = sourceMsg->m_destService;
		m_destMethod = sourceMsg->m_destMethod;
		m_size = RESPONSE_EXCEPTION_MESSAGE_SIZE;
	}

	void InitializeNext (Ptr<Message> sourceMsg, uint32_t destNode, uint32_t destService, uint32_t destMethod, uint32_t size)
	{
		NS_ASSERT(sourceMsg != NULL);

		m_messageType = MTRequest;
		m_messageId = ++s_messageCounter;
		m_relatedToMessageId = 0;
		m_conversationId = sourceMsg->m_conversationId;
		m_srcNode = sourceMsg->m_destNode;
		m_srcService = sourceMsg->m_destService;
		m_destNode = destNode;
		m_destService = destService;
		m_destMethod = destMethod;
		m_size = size;
	}

	uint32_t GetMessageType() const { return m_messageType; }
	uint32_t GetMessageId () const { return m_messageId; }
	uint32_t GetRelatedToMessageId () const { return m_relatedToMessageId; }
	uint32_t GetConversationId () const { return m_conversationId; }
	uint32_t GetSrcNode () const { return m_srcNode; }
	uint32_t GetSrcService () const { return m_srcService; }
	uint32_t GetDestNode () const { return m_destNode; }
	uint32_t GetDestService () const { return m_destService; }
	uint32_t GetDestMethod () const { return m_destMethod; }
	uint32_t GetSize () const { return m_size; }


	virtual TypeId GetInstanceTypeId (void) const { return GetTypeId (); }
	virtual uint32_t GetSerializedSize (void) const { return 40; }
	virtual void Print (std::ostream &os) const {}

	/*
	virtual void Print (std::ostream &os) const
	{
		os << "msg - type: " << m_messageType;
		os << " id: " << m_messageId;
		os << " relmsg: " << m_relatedToMessageId;
		os << " conversation: " << m_conversationId;
		os << " srcnode: " << m_srcNode;
		os << " srcservice: " << m_srcService;
		os << " destnode: " << m_destNode;
		os << " destservice: " << m_destService;
		os << " destmethod: " << m_destMethod;
	}
	*/

	void WriteOut ()
	{
		NS_LOG_UNCOND(
				"msg - type: " << m_messageType
			<< " id: " << m_messageId
			<< " relmsg: " << m_relatedToMessageId
			<< " conversation: " << m_conversationId
			<< " srcnode: " << m_srcNode
			<< " srcservice: " << m_srcService
			<< " destnode: " << m_destNode
			<< " destservice: " << m_destService
			<< " destmethod: " << m_destMethod
			);
	}

	static TypeId GetTypeId (void)
	{
		static TypeId tid = TypeId ("ns3::Message").SetParent<Header> ();

		return tid;
	}

	virtual void Serialize (Buffer::Iterator start) const
	{
		start.WriteU32 (m_messageType);
		start.WriteU32 (m_messageId);
		start.WriteU32 (m_relatedToMessageId);
		start.WriteU32 (m_conversationId);
		start.WriteU32 (m_srcNode);
		start.WriteU32 (m_srcService);
		start.WriteU32 (m_destNode);
		start.WriteU32 (m_destService);
		start.WriteU32 (m_destMethod);
		start.WriteU32 (m_size);
	}

	virtual uint32_t Deserialize (Buffer::Iterator start)
	{
		m_messageType = start.ReadU32();
		m_messageId = start.ReadU32();
		m_relatedToMessageId = start.ReadU32();
		m_conversationId = start.ReadU32();
		m_srcNode = start.ReadU32();
		m_srcService = start.ReadU32 ();
		m_destNode = start.ReadU32();
		m_destService = start.ReadU32 ();
		m_destMethod = start.ReadU32 ();
		m_size = start.ReadU32();
		return 40;
	}

	static uint32_t GetMessageCounter () { return s_messageCounter; }
	static uint32_t GetConversationCounter () { return s_conversationCounter; }

}; // Message

uint32_t Message::s_messageCounter = 0;
uint32_t Message::s_conversationCounter = 0;



#define MESSAGE_ACTION_SEND 			's'
#define MESSAGE_ACTION_RECEIVE 			'r'

#define ERROR_TYPE_SERVICE_PROCESSING 	"SERVICE_PROCESSING"
#define ERROR_TYPE_METHOD_PROCESSING 	"METHOD_PROCESSING"
#define ERROR_TYPE_RECEIVED_EXCEPTION	"RECEIVED_EXCEPTION"
#define ERROR_TYPE_RESPONSE_TIMEOUT		"RESPONSE_TIMEOUT"
#define ERROR_TYPE_ACK_TIMEOUT			"ACK_TIMEOUT"
#define ERROR_TYPE_SEND_FAILURE			"SEND_FAILURE"
#define ERROR_TYPE_SERVICE_NOT_FOUND	"SERVICE_NOT_FOUND"
#define ERROR_TYPE_SOCKET_FAILURE		"SOCKET_FAILURE"

class SimulationOutput : public Object
{
private:
	ofstream				m_msgStream;
	ofstream				m_errStream;
	ofstream				m_routingTablesStream;

	static uint32_t			s_errCounter;

public:

	SimulationOutput (const char* msgFileName, const char* errFileName, const char* routingTablesFileName)
	{
		NS_ASSERT(msgFileName != NULL);
		NS_ASSERT(errFileName != NULL);
		NS_ASSERT(routingTablesFileName != NULL);

		m_msgStream.open(msgFileName, ios::out);

		m_msgStream
			<< "timestamp,"
			<< "recordType,"
			<< "fromAddress,"
			<< "fromIp,"
			<< "fromPort,"
			<< "toAddress,"
			<< "toIp,"
			<< "toPort,"
			<< "msgMessageType,"
			<< "msgMessageId,"
			<< "msgRelatedToMessageId,"
			<< "msgConversationId,"
			<< "msgSrcNode,"
			<< "msgSrcService,"
			<< "msgDestNode,"
			<< "msgDestService,"
			<< "msgDestMethod,"
			<< "msgSize,"
			<< "retransmission,"
			<< "successSent,"
			<< "dropedDueToResent"
			<< '\r' << '\n';


		m_errStream.open(errFileName, ios::out);

		m_errStream
			<< "timestamp,"
			<< "serviceId,"
			<< "errorType,"
			<< "msgMessageId,"
			<< "note"
			<< '\r' << '\n';

		m_routingTablesStream.open(routingTablesFileName, ios::out);
	}

	virtual ~SimulationOutput()
	{
		m_msgStream.close();
		m_errStream.close();
		m_routingTablesStream.close();
	}

	void Flush ()
	{
		m_msgStream.flush();
		m_errStream.flush();
		m_routingTablesStream.flush();
	}

	static uint32_t GetErrCounter () { return s_errCounter; }

	void RecordError(uint32_t serviceId, const char* errorType, Ptr<Message> msg)
	{
		NS_ASSERT(errorType != NULL);
		NS_ASSERT(msg != NULL);

		s_errCounter++;

		m_errStream
			<< Simulator::Now().GetNanoSeconds() << ","
			<< serviceId << ","
			<< errorType << ","
			<< msg->GetMessageId() << ","
			<< '\r' << '\n';

		m_errStream.flush();
	}

	void RecordError(uint32_t serviceId, const char* errorType, Ptr<Message> msg, const char* note)
	{
		NS_ASSERT(errorType != NULL);
		NS_ASSERT(note != NULL);
		//NS_ASSERT(msg != NULL);
		uint32_t msgId = 0;


		if (msg != NULL)
		{
			msgId = msg->GetMessageId();
		}

		s_errCounter++;

		m_errStream
			<< Simulator::Now().GetNanoSeconds() << ","
			<< serviceId << ","
			<< errorType << ","
			<< msgId << ","
			<< note
			<< '\r' << '\n';

		m_errStream.flush();
	}

	void RecordSendMessage(
			Ptr<Message> msg,
			Address addressFrom,
			Address addressTo,
			uint32_t retransmission,
			bool successSent)
	{
		NS_ASSERT(msg != NULL);

		RecordMessage(
				MESSAGE_ACTION_SEND,
				msg,
				addressFrom,
				addressTo,
				retransmission,
				successSent,
				false);
	}

	void RecordReceiveMessage(
			Ptr<Message> msg,
			Address addressFrom,
			Address addressTo,
			bool dropedDueToResent)
	{
		NS_ASSERT(msg != NULL);

		RecordMessage(
				MESSAGE_ACTION_RECEIVE,
				msg,
				addressFrom,
				addressTo,
				0,
				false,
				dropedDueToResent);
	}

	static const char* GetSocketErrnoString (Ptr<Socket> socket)
	{
		NS_ASSERT(socket != NULL);

		switch(socket->GetErrno())
		{
			case Socket::ERROR_NOTERROR: return "ERROR_NOTERROR";
			case Socket::ERROR_ISCONN: return "ERROR_ISCONN";
			case Socket::ERROR_NOTCONN: return "ERROR_NOTCONN";
			case Socket::ERROR_MSGSIZE: return "ERROR_MSGSIZE";
			case Socket::ERROR_AGAIN: return "ERROR_AGAIN";
			case Socket::ERROR_SHUTDOWN: return "ERROR_SHUTDOWN";
			case Socket::ERROR_OPNOTSUPP: return "ERROR_OPNOTSUPP";
			case Socket::ERROR_AFNOSUPPORT: return "ERROR_AFNOSUPPORT";
			case Socket::ERROR_INVAL: return "ERROR_INVAL";
			case Socket::ERROR_BADF: return "ERROR_BADF";
			case Socket::ERROR_NOROUTETOHOST: return "ERROR_NOROUTETOHOST";
			case Socket::SOCKET_ERRNO_LAST: return "SOCKET_ERRNO_LAST";
			case Socket::ERROR_ADDRNOTAVAIL: return "ERROR_ADDRNOTAVAIL";
			case Socket::ERROR_NODEV: return "ERROR_NODEV";
			//case Socket::ERROR_ADDRINUSE: return "ERROR_ADDRINUSE";
		}

		return 0;
	}

	void RecordRoutingTable(Ptr<Node> node)
	{
		/*
		// Print  routing table entries for OLSR routing
        Ptr<RoutingProtocol> routing = node->GetObject<RoutingProtocol>();
        std::vector<RoutingTableEntry> entry = routing->GetRoutingTableEntries();


//      std::cout << "Routing table for device: " << Names::FindName(node) << std::endl;
//      m_routingTablesStream << "Routing table for device: " << node->GetId() << std::endl;

        for (std::vector<RoutingTableEntry,
        		std::allocator<RoutingTableEntry> >::iterator i=entry.begin();
        		i!=entry.end(); i++)
        {
        	if (i->destAddr != i->nextAddr)
        		m_routingTablesStream
        						<< Simulator::Now().GetNanoSeconds() << "- device:"
        						<< node->GetId() << "\t\t"
        						<< i->destAddr << "\t\t"
                                << i->nextAddr << "\t\t"
                                << i->interface << "\t\t"
                                << i->distance << std::endl;
        }
        */
	}

private:
	void RecordMessage(
			char recordType,
			Ptr<Message> msg,
			Address addressFrom,
			Address addressTo,
			uint32_t retransmission,
			bool successSent,
			bool dropedDueToResent)
	{
		NS_ASSERT(recordType != 0);
		NS_ASSERT(msg != NULL);

		InetSocketAddress from = InetSocketAddress::ConvertFrom(addressFrom);
		InetSocketAddress to = InetSocketAddress::ConvertFrom(addressTo);


		m_msgStream
			<< Simulator::Now().GetNanoSeconds() << ","
			<< recordType << ","
			<< addressFrom << ","
			<< from.GetIpv4() << ","
			<< from.GetPort() << ","
			<< addressTo << ","
			<< to.GetIpv4() << ","
			<< to.GetPort() << ","
			<< msg->GetMessageType() << ","
			<< msg->GetMessageId() << ","
			<< msg->GetRelatedToMessageId() << ","
			<< msg->GetConversationId() << ","
			<< msg->GetSrcNode() << ","
			<< msg->GetSrcService() << ","
			<< msg->GetDestNode() << ","
			<< msg->GetDestService() << ","
			<< msg->GetDestMethod() << ","
			<< msg->GetSize() << ","
			<< retransmission << ","
			<< (successSent ? 1 : 0) << ","
			<< (dropedDueToResent ? 1 : 0)
			<< '\r' << '\n';

		m_msgStream.flush();
	}

}; // SimulationOutput

uint32_t SimulationOutput::s_errCounter = 0;


class MessageEndpoint : public Object, public InstanceCounter
{
public:

	struct MessageTypeCounter
	{
		MessageTypeCounter ()
		{
			msgSendAttemptCounter = 0;
			msgSendSuccessCounter = 0;
			msgSendUniqueCounter = 0;
			msgReceiveCounter = 0;
			msgReceiveUniqueCounter = 0;
			msgSendFailureCounter = 0;
			msgACKTimeoutCounter = 0;
			msgResponseTimeoutCounter = 0;
		}

		uint32_t					msgSendAttemptCounter;
		uint32_t					msgSendSuccessCounter;
		uint32_t					msgSendUniqueCounter;
		uint32_t					msgReceiveCounter;
		uint32_t					msgReceiveUniqueCounter;
		uint32_t					msgSendFailureCounter;
		uint32_t					msgACKTimeoutCounter;
		uint32_t					msgResponseTimeoutCounter;
	};

private:

	Ptr<SimulationOutput>			m_simulationOutput;

	static MessageTypeCounter		s_msgCounters[];

protected:
	Ptr<Node> 						m_node;
	Ptr<ServiceBase>				m_serviceBase;


	MessageEndpoint (Ptr<Node> node, Ptr<ServiceBase> serviceBase, Ptr<SimulationOutput> simulationOutput)
	:InstanceCounter(typeid(this).name()),
	 m_simulationOutput(simulationOutput),
	 m_node(node),
	 m_serviceBase(serviceBase)
	{
		NS_ASSERT (node != NULL);
		NS_ASSERT (serviceBase != NULL);
		NS_ASSERT (simulationOutput != NULL);
	}

public:
	virtual ~MessageEndpoint() {}
	virtual void Open () = 0;
	virtual void Close () = 0;

	static MessageTypeCounter GetMessageCounter (uint32_t index) { return s_msgCounters[index]; }

protected:
	Ipv4Address GetNodeIP ()
	{
		Ptr<Ipv4> ipv4 = m_node->GetObject<Ipv4>();
		Ipv4InterfaceAddress iaddr = ipv4->GetAddress(1,0);


		return iaddr.GetLocal();
	}

	InetSocketAddress GetSocketAddress(uint16_t port)
	{
		return InetSocketAddress (GetNodeIP(), port);
	}

	void RecordSendMessage(Ptr<Socket> socket, Ptr<Message> msg, Address addressTo, uint32_t retransmission, bool success)
	{
		NS_ASSERT (msg != NULL);

		Address addressFrom = GetSocketAddress(0);


		s_msgCounters[msg->GetMessageType() -1].msgSendAttemptCounter++;
		if (success) s_msgCounters[msg->GetMessageType() -1].msgSendSuccessCounter++;
		if ((retransmission == 0) || (retransmission == 1)) s_msgCounters[msg->GetMessageType() -1].msgSendUniqueCounter++;

		m_simulationOutput->RecordSendMessage(msg, addressFrom, addressTo, retransmission, success);

		m_simulationOutput->RecordRoutingTable(socket->GetNode());

		if (!success)
		{
			const char * socketStatus = m_simulationOutput->GetSocketErrnoString(socket);
			m_simulationOutput->RecordError(m_serviceBase->GetServiceId(), ERROR_TYPE_SOCKET_FAILURE, msg, socketStatus);
		}
	}

	void RecordReceiveMessage(Ptr<Message> msg, Address addressFrom, bool dropedDueToResend)
	{
		NS_ASSERT (msg != NULL);

		Address addressTo = GetSocketAddress(0);


		s_msgCounters[msg->GetMessageType() -1].msgReceiveCounter++;
		if (!dropedDueToResend) s_msgCounters[msg->GetMessageType() -1].msgReceiveUniqueCounter++;

		m_simulationOutput->RecordReceiveMessage(msg, addressFrom, addressTo, dropedDueToResend);
	}

	void RecordSendFailure(Ptr<Message> msg)
	{
		NS_ASSERT (msg != NULL);

		s_msgCounters[msg->GetMessageType() -1].msgSendFailureCounter++;
		m_simulationOutput->RecordError(m_serviceBase->GetServiceId(), ERROR_TYPE_SEND_FAILURE, msg);
	}

	void RecordACKTimeout(Ptr<Message> msg)
	{
		NS_ASSERT (msg != NULL);

		s_msgCounters[msg->GetMessageType() -1].msgACKTimeoutCounter++;
		m_simulationOutput->RecordError(m_serviceBase->GetServiceId(), ERROR_TYPE_ACK_TIMEOUT, msg);
	}

	void RecordResponseTimeout(Ptr<Message> msg)
	{
		NS_ASSERT (msg != NULL);

		s_msgCounters[msg->GetMessageType() -1].msgResponseTimeoutCounter++;
		m_simulationOutput->RecordError(m_serviceBase->GetServiceId(), ERROR_TYPE_RESPONSE_TIMEOUT, msg);
	}

}; // MessageEndpoint

MessageEndpoint::MessageTypeCounter MessageEndpoint::s_msgCounters[] = {
		MessageTypeCounter(),
		MessageTypeCounter(),
		MessageTypeCounter(),
		MessageTypeCounter()};



class ClientMessageEndpoint : public MessageEndpoint
{
private:
	Callback<void> 						m_onSendSuccessCallback;
	Callback<void> 						m_onSendFailureCallback;
	Callback<void, Ptr<Message> > 		m_onReceiveResponseCallback;
	Callback<void> 						m_onResponseTimeoutCallback;

public:
	ClientMessageEndpoint (
			Ptr<Node> node,
			Ptr<ServiceBase> serviceBase,
			Ptr<SimulationOutput> simulationOutput,
			Callback<void> onSendSuccessCallback,
			Callback<void> onSendFailureCallback,
			Callback<void, Ptr<Message> > onReceiveResponseCallback,
			Callback<void> onResponseTimeoutCallback)
	: MessageEndpoint(node, serviceBase, simulationOutput),
	  m_onSendSuccessCallback(onSendSuccessCallback),
	  m_onSendFailureCallback(onSendFailureCallback),
	  m_onReceiveResponseCallback(onReceiveResponseCallback),
	  m_onResponseTimeoutCallback(onResponseTimeoutCallback)
	{
		NS_ASSERT (!onSendSuccessCallback.IsNull());
		NS_ASSERT (!onSendFailureCallback.IsNull());
		NS_ASSERT (!onReceiveResponseCallback.IsNull());
		NS_ASSERT (!onResponseTimeoutCallback.IsNull());
	}

	virtual ~ClientMessageEndpoint() {}
	virtual void SendMessage(Ptr<Message> msg, Address to, bool waitForResponse) = 0;

protected:

	void OnSendSuccess ()
	{
		m_onSendSuccessCallback();
	}

	void OnSendFailure ()
	{
		m_onSendFailureCallback();
	}

	void OnReceiveResponse (Ptr<Message> msg)
	{
		NS_ASSERT (msg != NULL);
		m_onReceiveResponseCallback(msg);
	}

	void OnResponseTimeout ()
	{
		m_onResponseTimeoutCallback();
	}

}; // ClientMessageEndpoint


class ServerMessageEndpoint : public MessageEndpoint
{
private:
	Callback<void, Ptr<Message>, Address> 		m_onReceiveRequest;

protected:
	uint16_t									m_port;

protected:

	ServerMessageEndpoint (
			Ptr<Node> node,
			Ptr<ServiceBase> serviceBase,
			Ptr<SimulationOutput> simulationOutput,
			Callback<void, Ptr<Message>, Address> onReceiveRequest,
			uint16_t port)
	: MessageEndpoint(node, serviceBase, simulationOutput),
	  m_onReceiveRequest(onReceiveRequest),
	  m_port(port)
	{
		NS_ASSERT (!onReceiveRequest.IsNull());
		NS_ASSERT (port > 0);
	}

public:
	virtual ~ServerMessageEndpoint() {}

	InetSocketAddress GetServerSocketAddress ()
	{
		return GetSocketAddress(m_port);
	}

protected:
	void OnReceiveRequest (Ptr<Message> msg, Address from)
	{
		NS_ASSERT (msg != NULL);
		m_onReceiveRequest(msg, from);
	}

}; // ServerMessageEndpoint






class EndpointMessageIdCache : public Object, public InstanceCounter
{
private:
	const Time						m_msgIdLifetime;
	map<uint32_t, Time> 			m_ids;
	EventId							m_removeOldRecordsEvent;

public:

	EndpointMessageIdCache (Ptr<ServiceBase> serviceBase)
	:InstanceCounter(typeid(this).name()),
	 m_msgIdLifetime(serviceBase->GetMsgIdLifetime())
	{
		// will start scheduled cache cleanup
		RemoveOldRecords();
	}

	virtual ~EndpointMessageIdCache()
	{
		m_removeOldRecordsEvent.Cancel();
	}

	void AddMessage(Ptr<Message> msg)
	{
		NS_ASSERT(msg);

		uint32_t 	msgId = msg->GetMessageId();
		Time		messageIdLifetime = m_msgIdLifetime + Simulator::Now();


		//if (m_ids.size() > 100)
		//NS_LOG_UNCOND("		EndpointMessageIdCache - m_ids : " << m_ids.size());

		m_ids.insert( pair<uint32_t, Time> (msgId, messageIdLifetime));
	}

	bool IsMessageInCache (Ptr<Message> msg)
	{
		NS_ASSERT(msg);

		uint32_t 		msgId = msg->GetMessageId();


		//NS_LOG_UNCOND("IsMessageInCache msgid: " << msgId << " ids size: " << m_ids.size());

		return (m_ids.count(msgId) > 0);
/*
		uint32_t 		msgId = msg->GetMessageId();
		bool			isInCache = false;
		map<uint32_t, Time>::iterator			it;



		if (m_ids.size() > 0)
		{
			//isInCache = (m_ids.find(msgId) != m_ids.end());

			it = m_ids.find(msgId);
			isInCache = (it != m_ids.end());
		}

		return isInCache;
		*/
	}

	bool HaveMessageAlreadyArrived (Ptr<Message> msg)
	{
		NS_ASSERT(msg);

		bool result = IsMessageInCache(msg);


		if (!result)
		{
			AddMessage(msg);
		}

		return result;
	}

	void RemoveOldRecords()
	{
		map<uint32_t, Time>::iterator			it;
		//uint32_t								sizeBefore = m_ids.size();


		for (it = m_ids.begin(); it != m_ids.end(); it++)
		{
			if(it->second < Simulator::Now())
			{
					m_ids.erase(it->first);
			}
		}

		m_removeOldRecordsEvent = Simulator::Schedule (
				MilliSeconds(1000),
				&EndpointMessageIdCache::RemoveOldRecords,
				this);

		//NS_LOG_UNCOND("		RemoveOldRecords : start:" << sizeBefore << " end: " << m_ids.size());

	}

}; // EndpointMessageIdCache





class UdpClientSocket : public Object
{
private:
	Ptr<Socket>						m_socket;
	Ptr<Node>						m_node;
	bool							m_isInUse;
	Callback<void, Ptr<Socket> >	m_onReceiveMessageCallback;

public:

	UdpClientSocket (Ptr<Node> node)
		:m_node(node),
		 m_isInUse(false)
	{
		NS_ASSERT (m_node != NULL);

		//NS_LOG_UNCOND("Opening new client socket: node id" << m_node->GetId());

		Open();
	}

	virtual ~UdpClientSocket ()
	{
		Close();
	}

	Ptr<Socket> GetNS3Socket ()
	{
		NS_ASSERT (m_isInUse == true);

		return m_socket;
	}

	void LockForMessageEndpoint ()
	{
		NS_ASSERT (m_isInUse == false);

		//NS_LOG_UNCOND("Locking socket: node id" << m_node->GetId());

		m_isInUse = true;
	}

	void ReleaseBackToPool ()
	{
		NS_ASSERT (m_isInUse == true);

		//NS_LOG_UNCOND("Releasing socket: node id" << m_node->GetId());

		m_isInUse = false;
	}

	void SetReceiveMessageCallback (Callback<void, Ptr<Socket> > onReceiveMessageCallback)
	{
		m_onReceiveMessageCallback = onReceiveMessageCallback;
	}

	bool GetIsInUse ()
	{
		return m_isInUse;
	}

private:

	void ReceiveMessage (Ptr<Socket> socket)
	{
		if (m_isInUse)
		{
			//!is it because of timeout???
			try
			{
				m_onReceiveMessageCallback(socket);
			}
			catch (exception& e)
			{
				NS_LOG_UNCOND("UdpClientSocket::ReceiveMessage - exception: " << e.what());
			}
			catch (...)
			{
				NS_LOG_UNCOND("UdpClientSocket::ReceiveMessage - default exception");
			}
		}
	}

	void Open ()
	{
		NS_ASSERT (m_socket == NULL);

		int result;

		m_socket = Socket::CreateSocket (m_node, UdpSocketFactory::GetTypeId ());
		result = m_socket->Bind ();
		NS_ASSERT (result == 0);
		m_socket->SetRecvCallback (MakeCallback(&UdpClientSocket::ReceiveMessage, this));
	}

	void Close ()
	{
		if (m_socket != NULL)
		{
			m_socket->Close();
			m_socket = NULL;
		}
	}
}; // UdpClientSocket


class UdpClientNodeSocketPool : public Object
{
private:
	list<Ptr<UdpClientSocket> >			m_sockets;
	Ptr<Node>							m_node;

public:

	UdpClientNodeSocketPool (Ptr<Node> node)
	:m_node(node)
	{
		NS_ASSERT(node != NULL);
	}

	virtual ~UdpClientNodeSocketPool ()
	{}

	Ptr<UdpClientSocket> GetSocketFromPool()
	{
		Ptr<UdpClientSocket> socket = GetFreeSocketFromPool();


		socket->LockForMessageEndpoint();

		return socket;
	}

private:

	Ptr<UdpClientSocket> GetFreeSocketFromPool()
	{
		list<Ptr<UdpClientSocket> >::iterator it;
		Ptr<UdpClientSocket> socket;


		for (it=m_sockets.begin(); it != m_sockets.end(); it++)
		{
			socket = *it;

			if (!socket->GetIsInUse())
			{
				return socket;
			}
		}

		socket = CreateNewSocketAndAddItIntoPool();

		return socket;
	}

	Ptr<UdpClientSocket> CreateNewSocketAndAddItIntoPool()
	{
		Ptr<UdpClientSocket> socket = CreateObject<UdpClientSocket>(m_node);


		m_sockets.push_back (socket);

		return socket;
	}
}; // UdpClientNodeSocketPool



class UdpClientSocketPool : public Object
{
private:
	map<uint32_t, Ptr<UdpClientNodeSocketPool> > 	m_nodeSocketPools;

	static Ptr<UdpClientSocketPool>					s_pool;

public:

	UdpClientSocketPool ()
	{}

	virtual ~UdpClientSocketPool ()
	{}

	static Ptr<UdpClientSocketPool> GetPool ()
	{
		if (s_pool == NULL)
		{
			s_pool = CreateObject<UdpClientSocketPool>();
		}

		return s_pool;
	}

	Ptr<UdpClientSocket> GetSocketFromPool(Ptr<Node> node)
	{
		NS_ASSERT(node != NULL);

		Ptr<UdpClientNodeSocketPool> 	nodeSocketPool = GetNodeSocketPool(node);
		Ptr<UdpClientSocket>			socket;


		socket = nodeSocketPool->GetSocketFromPool();

		return socket;
	}

private:

	Ptr<UdpClientNodeSocketPool> GetNodeSocketPool(Ptr<Node> node)
	{
		map<uint32_t, Ptr<UdpClientNodeSocketPool> >::iterator 		it;
		Ptr<UdpClientNodeSocketPool> 								nodeSocketPool;


		it = m_nodeSocketPools.find(node->GetId());

		if (it != m_nodeSocketPools.end())
		{
			nodeSocketPool = it->second;
		}
		else
		{
			nodeSocketPool = CreateNewNodeSocketPoolAndAddItIntoMap(node);
		}

		return nodeSocketPool;
	}

	Ptr<UdpClientNodeSocketPool> CreateNewNodeSocketPoolAndAddItIntoMap(Ptr<Node> node)
	{
		Ptr<UdpClientNodeSocketPool> nodeSocketPool = CreateObject<UdpClientNodeSocketPool>(node);


		m_nodeSocketPools.insert (
				pair<
					uint32_t,
					Ptr<UdpClientNodeSocketPool> > (
						node->GetId(),
						nodeSocketPool) );

		return nodeSocketPool;
	}
}; // UdpClientSocketPool

Ptr<UdpClientSocketPool> UdpClientSocketPool::s_pool;



#define REPORT_ENDPOINT_CHANGE(prefix, service, state) // NS_LOG_UNCOND(prefix << " " << service->GetServiceId() << " " << state)
#define REPORT_ENDPOINT_MSG(msg) // if (msg != NULL) msg->WriteOut()


class UdpClientMessageEndpoint : public ClientMessageEndpoint
{
private:
	Ptr<UdpClientSocket>	m_clientSocket;
	Ptr<Socket>				m_socket;
	EventId					m_ACKTimeoutEvent;
	EventId					m_socketTimeoutEvent;
	EventId					m_responseTimeoutEvent;
	Ptr<Message>			m_requestMessage;
	Address					m_requestAddress;
	Ptr<Message>			m_responseMessage;
	Address					m_responseAddress;
	bool					m_waitForResponse;
	uint32_t				m_retransmissionCounter;
	Ptr<EndpointMessageIdCache>		m_msgCache;

/*
	static uint32_t		counter;
	static uint32_t		currentCount;
	uint32_t 			id;
*/

public:

	UdpClientMessageEndpoint (
			Ptr<Node> node,
			Ptr<ServiceBase> serviceBase,
			Ptr<SimulationOutput> simulationOutput,
			Callback<void> onSendSuccessCallback,
			Callback<void> onSendFailureCallback,
			Callback<void, Ptr<Message> > onReceiveResponseCallback,
			Callback<void> onResponseTimeoutCallback)
	: ClientMessageEndpoint(
			node,
			serviceBase,
			simulationOutput,
			onSendSuccessCallback,
			onSendFailureCallback,
			onReceiveResponseCallback,
			onResponseTimeoutCallback)
	{
		/*
		counter++;
		id = counter;
		currentCount++;
		NS_LOG_UNCOND("opening: " << id << " currentcount: " << currentCount << " total: " << counter << " parent: " << m_serviceBase->GetServiceId());
*/

		m_msgCache = CreateObject<EndpointMessageIdCache>(serviceBase);
	}

	virtual ~UdpClientMessageEndpoint()
	{
		//currentCount--;
		//NS_LOG_UNCOND("closing: " << id << " currentcount: " << currentCount << " total: " << counter << " parent: " << m_serviceBase->GetServiceId());
		Close();
	}

	virtual void Open ()
	{
		m_clientSocket = UdpClientSocketPool::GetPool()->GetSocketFromPool(m_node);
		m_clientSocket->SetReceiveMessageCallback(MakeCallback(&UdpClientMessageEndpoint::ReceiveMessage, this));
		m_socket = m_clientSocket->GetNS3Socket();
	}

	virtual void Close ()
	{
		if (m_socket != NULL)
		{
			m_clientSocket->ReleaseBackToPool();
			m_clientSocket = NULL;
			m_socket = NULL;
		}

		Socket_CancelTimeout();
		ACK_CancelTimeout();
		Response_CancelTimeout();
	}

	virtual void SendMessage(Ptr<Message> msg, Address to, bool waitForResponse)
	{
		NS_ASSERT(msg != NULL);
		NS_ASSERT(m_socket != NULL);

		m_requestMessage = msg;
		m_requestAddress = to;
		m_waitForResponse = waitForResponse;
		m_responseMessage = NULL;

		Transition_StartSendMessage();
	}

private:

	void ReceiveMessage (Ptr<Socket> socket)
	{
		NS_ASSERT (socket != NULL);

	  	Ptr<Packet> 		packet;
		Address 			from;
		Ptr<Message> 		msg = CreateObject<Message>();
		bool 				haveMsgAlreadyArrived;


		REPORT_ENDPOINT_CHANGE("client", m_serviceBase, "ReceiveMessage");

		while (packet = socket->RecvFrom (from))
		{
			if (packet->GetSize () > 0)
			{

				/*
				 * 1) check if the message is relevant response (or ack)
				 * 	- if not drop it - and continue on next message on socket
				 * 2) check if the message already arrived
				 * - if yes drop it - and continue on next message
				 * 3) because there might be more than one message at one time (problem with closed endpoint and processing next message)
				 * - if the message is ACK process message and continue on next message on socket
				 * - if the message is reponse empty the socket to allow the socket to close/return to pool
				 * */


				packet->RemoveHeader (*msg);

				// check if the response is related to the current request, if not drop it
				if ((m_requestMessage == NULL) || (msg->GetRelatedToMessageId() != m_requestMessage->GetMessageId()))
				{
					/*
					NS_LOG_UNCOND (
							"UpdClientMessageEndpoint received unexpected message - msg id : "
							<< msg->GetMessageId()
							<< " message discarded");
					*/

					continue;
				}

				haveMsgAlreadyArrived = m_msgCache->HaveMessageAlreadyArrived(msg);

				// the messages eliminated by above conditions
				// wont be observed by monitors in chain of sinks
				RecordReceiveMessage(msg, from, haveMsgAlreadyArrived);

				if (!haveMsgAlreadyArrived)
				{
					Task_ProcessReceivedMessage(msg, from);

					switch(msg->GetMessageType())
					{
						case Message::MTACK:
							continue; // if ACK try if there is more messages
							break;

						case Message::MTResponse:
						case Message::MTResponseException:
							// if response - empty socket in order to release it back to pool

							while (packet = socket->RecvFrom (from))
							{
								// just read it in order to empty the socket
								NS_LOG_UNCOND ("message dropped on socket as beeing after response message");
								msg->WriteOut();

								if (packet->GetSize () > 0)
								{
									packet->RemoveHeader (*msg);
									msg->WriteOut();
								}
							}

							return;

							break;

						default:
							// if it comes here, something wrong is happening
							NS_LOG_UNCOND ("messs !!");
							msg->WriteOut();
							break;
					}
				}
			}
		}
	}

	// Tasks

	bool Task_SendMessage (Ptr<Message> msg, Address to, uint32_t retransmissionCounter)
	{
		NS_ASSERT (msg != NULL);
		NS_ASSERT (m_socket != NULL);
		REPORT_ENDPOINT_MSG(msg);


		uint32_t 		size = msg->GetSize();
		Ptr<Packet> 	packet = Create<Packet> (size);
		uint32_t 		sendResult;
		bool			sendSuccess;


		packet->AddHeader (*msg);
		sendResult = m_socket->SendTo (packet, 0, to);
		sendSuccess = (sendResult > 0);

		RecordSendMessage(m_socket, msg, to, retransmissionCounter, sendSuccess);

		return sendSuccess;
	}

	bool Task_SendMessage ()
	{
		REPORT_ENDPOINT_CHANGE("client", m_serviceBase, "Task_SendMessage");

		m_retransmissionCounter++;
		return Task_SendMessage(m_requestMessage, m_requestAddress, m_retransmissionCounter);
	}

	void Task_SendACK ()
	{
		REPORT_ENDPOINT_CHANGE("client", m_serviceBase, "Task_SendACK");

		Ptr<Message> ack = CreateObject<Message>();


		ack->InitializeACK(m_responseMessage);
		//m_retransmissionCounter = 0;

		Task_SendMessage(ack, m_responseAddress, 0);
	}

	void Task_ProcessReceivedMessage(Ptr<Message> msg, Address from)
	{
		REPORT_ENDPOINT_CHANGE("client", m_serviceBase, "Task_ProcessReceivedMessage");
		REPORT_ENDPOINT_MSG(msg);

		switch(msg->GetMessageType())
		{
			case Message::MTACK:
				Transition_ReceivedACK();
				break;

			default:
				m_responseMessage = msg;
				m_responseAddress = from;
				Transition_ReceivedResponse();
		}
	}


	// State machine - States and Transitions

	// First part - Sending of Request

	void Transition_StartSendMessage ()
	{
		REPORT_ENDPOINT_CHANGE("client", m_serviceBase, "Transition_StartSendMessage");

		Socket_CancelTimeout();
		ACK_CancelTimeout();
		Response_CancelTimeout();

		m_retransmissionCounter = 0;
		Response_StartTimeout(); // has to start at beggining of communication, the network has timeout period to send/receive etc, to do all whats needed
								// this is in fact application layer function rather then network, thus its running regardless of other net timeouts etc

		State_SendingRequest();
	}

	void State_SendingRequest ()
	{
		bool			sendSuccess;


		REPORT_ENDPOINT_CHANGE("client", m_serviceBase, "State_SendingRequest");

		sendSuccess = Task_SendMessage();

		if (sendSuccess)
		{
			Transition_RequestSentSuccessfully();
		}
		else
		{
			Transition_SocketSendFailed();
		}
	}

	void Transition_RequestSentSuccessfully ()
	{
		REPORT_ENDPOINT_CHANGE("client", m_serviceBase, "Transition_RequestSentSuccessfully");

		State_WaitForACK();
	}

	void Transition_SocketSendFailed ()
	{
		REPORT_ENDPOINT_CHANGE("client", m_serviceBase, "Transition_SocketSendFailed");
		State_SocketResending();
	}

	void State_WaitForACK()
	{
		REPORT_ENDPOINT_CHANGE("client", m_serviceBase, "State_WaitForACK");
		ACK_StartTimeout();
	}

	void State_SocketResending()
	{
		REPORT_ENDPOINT_CHANGE("client", m_serviceBase, "State_SocketResending");

		if (m_retransmissionCounter >= m_serviceBase->GetRetransmissionLimit())
		{
			Transition_RetransmissionLimitReached();
			return;
		}

		Socket_StartTimeout();
	}

	void Transition_SocketTimeout ()
	{
		REPORT_ENDPOINT_CHANGE("client", m_serviceBase, "Transition_SocketTimeout");
		State_SendingRequest();
	}

	void Transition_ACKTimeout ()
	{
		REPORT_ENDPOINT_CHANGE("client", m_serviceBase, "Transition_ACKTimeout");

		if (m_retransmissionCounter >= m_serviceBase->GetRetransmissionLimit())
		{
			Transition_RetransmissionLimitReached();
			return;
		}

		State_SendingRequest();
	}

	void Transition_ReceivedACK ()
	{
		REPORT_ENDPOINT_CHANGE("client", m_serviceBase, "Transition_ReceivedACK");

		// prevent receiving ACK more then once
		if (!m_ACKTimeoutEvent.IsRunning()) return;

		Socket_CancelTimeout();
		ACK_CancelTimeout();

		OnSendSuccess();

		// end of processing if not m_waitForResponse

		if (m_waitForResponse)
		{
			State_WaitingForResponse();
		}
	}


	// Part two - Receiving of Response

	void State_WaitingForResponse ()
	{
		REPORT_ENDPOINT_CHANGE("client", m_serviceBase, "State_WaitingForResponse");

		//Response_StartTimeout();
		// starts with succesful request
	}

	void Transition_ReceivedResponse ()
	{
		REPORT_ENDPOINT_CHANGE("client", m_serviceBase, "Transition_ReceivedResponse");

		Socket_CancelTimeout();
		ACK_CancelTimeout(); // just for sure - ACK may have not arrived
		Response_CancelTimeout();

		State_HavingResponse();
	}

	// Final failure states
	void Transition_ResponseTimeout ()
	{
		REPORT_ENDPOINT_CHANGE("client", m_serviceBase, "Transition_ResponseTimeout");

		Socket_CancelTimeout();
		ACK_CancelTimeout(); // just for sure
		Response_CancelTimeout();

		OnResponseTimeout();
	}

	void Transition_RetransmissionLimitReached ()
	{
		REPORT_ENDPOINT_CHANGE("client", m_serviceBase, "Transition_RetransmissionLimitReached");

		Socket_CancelTimeout();
		ACK_CancelTimeout(); // just for sure
		Response_CancelTimeout();

		RecordSendFailure(m_requestMessage);
		OnSendFailure();
	}

	// Final success state
	void State_HavingResponse()
	{
		REPORT_ENDPOINT_CHANGE("client", m_serviceBase, "State_HavingResponse");

		Socket_CancelTimeout();
		ACK_CancelTimeout(); // just for sure
		Response_CancelTimeout();

		Task_SendACK();
		OnReceiveResponse(m_responseMessage);
	}


	// timing
	void Socket_StartTimeout ()
	{
		//if (id == 24){
		//NS_LOG_UNCOND("socket start: " << id << " currentcount: " << currentCount << " total: " << counter << " parent: " << m_serviceBase->GetServiceId());
		//m_requestMessage->WriteOut();}

		m_socketTimeoutEvent = Simulator::Schedule (
				m_serviceBase->GetACKTimeout(),
				&UdpClientMessageEndpoint::Socket_TimeoutExpired,
				this);
	}

	void Socket_CancelTimeout ()
	{
		/*
		if (id == 24){
		NS_LOG_UNCOND("socket cancel: " << id << " currentcount: " << currentCount << " total: " << counter << " parent: " << m_serviceBase->GetServiceId());
		m_requestMessage->WriteOut();}
*/
		m_socketTimeoutEvent.Cancel();
	}

	void Socket_TimeoutExpired ()
	{
		/*
		if (id == 24){
		NS_LOG_UNCOND("socket expired: " << id << " currentcount: " << currentCount << " total: " << counter << " parent: " << m_serviceBase->GetServiceId());
		m_requestMessage->WriteOut();}
*/

		Socket_CancelTimeout();
		Transition_SocketTimeout();
	}

	void ACK_StartTimeout ()
	{
		/*
		if (id == 24){
		NS_LOG_UNCOND("ack start: " << id << " currentcount: " << currentCount << " total: " << counter << " parent: " << m_serviceBase->GetServiceId());
		m_requestMessage->WriteOut();}
*/

		m_ACKTimeoutEvent = Simulator::Schedule (
				m_serviceBase->GetACKTimeout(),
				&UdpClientMessageEndpoint::ACK_TimeoutExpired,
				this);
	}

	void ACK_CancelTimeout ()
	{
		/*
		if (id == 24){
		NS_LOG_UNCOND("ack cancel: " << id << " currentcount: " << currentCount << " total: " << counter << " parent: " << m_serviceBase->GetServiceId());
		m_requestMessage->WriteOut();}
*/

		m_ACKTimeoutEvent.Cancel();
	}

	void ACK_TimeoutExpired ()
	{
		/*
		if (id == 24){
		NS_LOG_UNCOND("ace expired: " << id << " currentcount: " << currentCount << " total: " << counter << " parent: " << m_serviceBase->GetServiceId());
		m_requestMessage->WriteOut();}
*/

		ACK_CancelTimeout();
		RecordACKTimeout(m_requestMessage);
		Transition_ACKTimeout();
	}

	void Response_StartTimeout ()
	{
		/*
		if (id == 24){
		NS_LOG_UNCOND("start timeout: " << id << " currentcount: " << currentCount << " total: " << counter << " parent: " << m_serviceBase->GetServiceId());
		m_requestMessage->WriteOut();}
*/

		Response_CancelTimeout();

		m_responseTimeoutEvent = Simulator::Schedule (
				m_serviceBase->GetResponseTimeout(),
				&UdpClientMessageEndpoint::Response_TimeoutExpired,
				this);
	}

	void Response_CancelTimeout ()
	{
		/*
		if (id == 24){
		NS_LOG_UNCOND("cancel timeout: " << id << " currentcount: " << currentCount << " total: " << counter << " parent: " << m_serviceBase->GetServiceId());
		m_requestMessage->WriteOut();
		}
*/
		m_responseTimeoutEvent.Cancel();
	}

	void Response_TimeoutExpired ()
	{
		//if (id == 24)
		//NS_LOG_UNCOND("Response_TimeoutExpired: " << id << " currentcount: " << currentCount << " total: " << counter << " parent: " << m_serviceBase->GetServiceId());


		Response_CancelTimeout();
		RecordResponseTimeout(m_requestMessage);
		Transition_ResponseTimeout();

		//m_requestMessage->WriteOut();

	}

}; // UdpClientMessageEndpoint

//uint32_t UdpClientMessageEndpoint::counter = 0;
//uint32_t UdpClientMessageEndpoint::currentCount = 0;



class UdpServerMessageEndpoint : public ServerMessageEndpoint
{
private:
	Ptr<Socket>						m_socket;
	Ptr<EndpointMessageIdCache>		m_msgCache;

public:

	UdpServerMessageEndpoint (
			Ptr<Node> node,
			Ptr<ServiceBase> serviceBase,
			Ptr<SimulationOutput> simulationOutput,
			Callback<void, Ptr<Message>, Address> onReceiveRequest,
			uint16_t port)
		:ServerMessageEndpoint(node, serviceBase, simulationOutput, onReceiveRequest, port)
	{
		m_msgCache = CreateObject<EndpointMessageIdCache>(serviceBase);
	}

	virtual ~UdpServerMessageEndpoint()
	{
		Close();
	}

	virtual void Open ()
	{
		NS_ASSERT (m_socket == NULL);

		int result;

		m_socket = Socket::CreateSocket (m_node, UdpSocketFactory::GetTypeId ());
		result = m_socket->Bind (InetSocketAddress (Ipv4Address::GetAny (), m_port));
		NS_ASSERT (result == 0);
		m_socket->SetRecvCallback (MakeCallback(&UdpServerMessageEndpoint::ReceiveRequest, this));
	}

	virtual void Close ()
	{
		if (m_socket != NULL)
		{
			m_socket->Close();
			m_socket = 0;
		}
	}

private:

	void ReceiveRequest (Ptr<Socket> socket)
	{
		NS_ASSERT (socket != NULL);

		Ptr<Packet> packet;
		Address from;
		Ptr<Message> msg = CreateObject<Message>();
		bool haveMsgAlreadyArrived;


		while (packet = socket->RecvFrom (from))
		{
			if (packet->GetSize () > 0)
			{
				packet->RemoveHeader (*msg);
				haveMsgAlreadyArrived = m_msgCache->HaveMessageAlreadyArrived(msg);

				RecordReceiveMessage(msg, from, haveMsgAlreadyArrived);

				REPORT_ENDPOINT_CHANGE ("server", m_serviceBase, "ReceiveRequest");
				REPORT_ENDPOINT_MSG(msg);

				SendACK(msg, from);

				// eliminate repeated requests
				if (!haveMsgAlreadyArrived)
				{
					OnReceiveRequest(msg, from);
				}
			}
		}
	}

	void SendACK (Ptr<Message> msg, Address to)
	{
		NS_ASSERT (msg != NULL);
		NS_ASSERT (m_socket != NULL);

		uint32_t 		size = msg->GetSize();
		Ptr<Packet> 	packet = Create<Packet> (size);
		uint32_t 		sendResult;
		bool			sendSuccess;
		Ptr<Message>	msgACK = CreateObject<Message>();


		msgACK->InitializeACK(msg);
		packet->AddHeader (*msgACK);

		sendResult = m_socket->SendTo (packet, 0, to);
		sendSuccess = (sendResult > 0);
		RecordSendMessage(m_socket, msgACK, to, 0, sendSuccess);

		REPORT_ENDPOINT_CHANGE("server", m_serviceBase, "SendACK");
		REPORT_ENDPOINT_MSG(msgACK);
	}


}; // UdpServerMessageEndpoint


class MessageEndpointFactory
{
public:
	static Ptr<ClientMessageEndpoint> CreateClientMessageEndpoint(
			Ptr<Node> node,
			Ptr<ServiceBase> serviceBase,
			Ptr<SimulationOutput> simulationOutput,
			Callback<void> onSendSuccessCallback,
			Callback<void> onSendFailureCallback,
			Callback<void, Ptr<Message> > onReceiveResponseCallback,
			Callback<void> onResponseTimeoutCallback)
	{
		NS_ASSERT(node != NULL);
		NS_ASSERT(serviceBase != NULL);
		NS_ASSERT(simulationOutput != NULL);
		NS_ASSERT(!onSendSuccessCallback.IsNull());
		NS_ASSERT(!onSendFailureCallback.IsNull());
		NS_ASSERT(!onReceiveResponseCallback.IsNull());
		NS_ASSERT(!onResponseTimeoutCallback.IsNull());

		return CreateObject<UdpClientMessageEndpoint> (
				node,
				serviceBase,
				simulationOutput,
				onSendSuccessCallback,
				onSendFailureCallback,
				onReceiveResponseCallback,
				onResponseTimeoutCallback);
	}

	static Ptr<ServerMessageEndpoint> CreateServerMessageEndpoint(
			Ptr<Node> node,
			Ptr<ServiceBase> serviceBase,
			Ptr<SimulationOutput> simulationOutput,
			Callback<void, Ptr<Message>, Address> onReceiveRequest,
			uint16_t port)
	{
		NS_ASSERT(node != NULL);
		NS_ASSERT(simulationOutput != NULL);
		NS_ASSERT(!onReceiveRequest.IsNull());

		return CreateObject<UdpServerMessageEndpoint> (
				node,
				serviceBase,
				simulationOutput,
				onReceiveRequest,
				port);
	}

}; // MessageEndpointFactory






















/****************************************************************************************
 * Execution model - Service Layer
 *
 * defines following:
 * - ServiceRegistry
 * - RunningTaskManager					- manager of tasks running in single service / service thread collection
 * - RequestProcessingTask				- pseudo thread - processing of single request in service - instantiated per request
 * - ServiceInstance
 * - ClientInstance
 *
 * */



class ServiceRegistryRecord : public Object
{
private:
	Ptr<Service> 		m_service;
	Address 			m_serviceAddress;
	uint32_t 			m_nodeId;

public:

	ServiceRegistryRecord (
			Ptr<Service> service,
			Address serviceAddress,
			uint32_t nodeId)
	:m_service(service),
	 m_serviceAddress(serviceAddress),
	 m_nodeId(nodeId)
	{
		NS_ASSERT(service != NULL);
	}

	virtual ~ServiceRegistryRecord ()
	{}

	Ptr<Service> GetService ()
	{
		return m_service;
	}

	uint32_t GetNodeId ()
	{
		return m_nodeId;
	}

	Address GetServiceAddress ()
	{
		return m_serviceAddress;
	}

}; // ServiceRegistryRecord




class ServiceRegistryServiceSelector : public Object
{
public:

	virtual Ptr<ServiceRegistryRecord> SelectService (
			Ptr<Node> srcNode,
			uint32_t destContractId,
			set<Ptr<ServiceRegistryRecord> > destRecords) = 0;

}; // ServiceRegistryServiceSelector


class ServiceRegistryServiceSelectorHopDistance : public ServiceRegistryServiceSelector
{
public:

	virtual Ptr<ServiceRegistryRecord> SelectService (
			Ptr<Node> srcNode,
			uint32_t destContractId,
			set<Ptr<ServiceRegistryRecord> > destRecords)
	{
		set<Ptr<ServiceRegistryRecord> >::iterator 		it;
		Ptr<ServiceRegistryRecord>						record;
		uint32_t										recordDistance;
		uint32_t										nearestRecordDistance = 1000;
		Ptr<ServiceRegistryRecord>						nearestRecord;


		// set first record as default
		nearestRecord = *destRecords.begin();

		//NS_LOG_UNCOND("msg to destContractId: " << destContractId);

		for (it=destRecords.begin(); it!=destRecords.end(); it++)
		{
			record = *it;
			recordDistance = GetHopDistanceOfNode(srcNode, record->GetServiceAddress());

			//NS_LOG_UNCOND(" node: " << record->GetNodeId() << " distance: " << recordDistance);

			if (recordDistance > 0 && recordDistance < nearestRecordDistance)
			{
				nearestRecord = record;
				nearestRecordDistance = recordDistance;
			}
		}

		//NS_LOG_UNCOND(" selected node: " << nearestRecord->GetNodeId());

		return nearestRecord;
	}

	int GetHopDistanceOfNode(Ptr<Node> srcNode, Address destServiceAddress)
	{
		Ptr<RoutingProtocol> 					routing = srcNode->GetObject<RoutingProtocol>();
        vector<RoutingTableEntry> 				entry = routing->GetRoutingTableEntries();
        vector<RoutingTableEntry>::iterator 	it;
        InetSocketAddress						destNodeAddress = InetSocketAddress::ConvertFrom(destServiceAddress);


		//NS_LOG_UNCOND("routing table for node: " << srcNode->GetId() << " dest node: " << destNodeAddress.GetIpv4());

        for (it=entry.begin(); it!=entry.end(); it++)
        {
    		//NS_LOG_UNCOND("destination node: " << it->destAddr << " distance: " << it->distance);

        	if (it->destAddr == destNodeAddress.GetIpv4())
        	{
        		//NS_LOG_UNCOND("found == ");

        		return it->distance;
        	}
        }

        return 0;
	}

}; // ServiceRegistryServiceSelectorHopDistance


class ServiceRegistryServiceSelectorPhysicalDistance : public ServiceRegistryServiceSelector
{
public:

	virtual Ptr<ServiceRegistryRecord> SelectService (
			Ptr<Node> srcNode,
			uint32_t destContractId,
			set<Ptr<ServiceRegistryRecord> > destRecords)
	{
		Ptr<MobilityModel> 								srcMm = srcNode->GetObject<MobilityModel>();
		Ptr<Node> 										destNode;
		Ptr<MobilityModel>								destMm;
		set<Ptr<ServiceRegistryRecord> >::iterator 		it;
		Ptr<ServiceRegistryRecord>						record;
		double											recordDistance;
		double											nearestRecordDistance = 1000;
		Ptr<ServiceRegistryRecord>						nearestRecord;


		// set first record as default
		nearestRecord = *destRecords.begin();

		//NS_LOG_UNCOND("msg to destContractId: " << destContractId);

		for (it=destRecords.begin(); it!=destRecords.end(); it++)
		{
			record = *it;
			destNode = NodeContainer::GetGlobal().Get(record->GetNodeId());
			destMm = destNode->GetObject<MobilityModel>();

			recordDistance = CalculateDistance(
				srcMm->GetPosition(),
				destMm->GetPosition());

			//NS_LOG_UNCOND(" src position: " << srcMm->GetPosition() << " dest position: " << destMm->GetPosition());
			//NS_LOG_UNCOND(" node: " << record->GetNodeId() << " distance: " << recordDistance);

			if (recordDistance < nearestRecordDistance)
			{
				nearestRecord = record;
				nearestRecordDistance = recordDistance;
			}
		}

		//NS_LOG_UNCOND(" selected node: " << nearestRecord->GetNodeId());

		return nearestRecord;
	}

}; // ServiceRegistryServiceSelectorPhysicalDistance


class ServiceRegistryServiceSelectorSingleService : public ServiceRegistryServiceSelector
{
public:

	virtual Ptr<ServiceRegistryRecord> SelectService (
			Ptr<Node> srcNode,
			uint32_t destContractId,
			set<Ptr<ServiceRegistryRecord> > destRecords)
	{
		return *destRecords.begin();
	}

}; // ServiceRegistryServiceSelectorSingleService





class ServiceRegistry
{
private:

	// key is serviceId
	static map <uint32_t, Ptr<ServiceRegistryRecord> > 			s_serviceRecords;

	// key is contractId
	static multimap <uint32_t, Ptr<ServiceRegistryRecord> > 	s_contractRecords;

	static Ptr<ServiceRegistryServiceSelector>					s_serviceSelector;


public:

	static void Initialize (Ptr<ServiceRegistryServiceSelector> serviceSelector)
	{
		NS_ASSERT(serviceSelector != NULL);

		s_serviceSelector = serviceSelector;
	}

	static void RegisterService (
			Ptr<Service> service,
			Address serviceAddress,
			uint32_t nodeId)
	{
		NS_ASSERT(service != NULL);

		Ptr<ServiceRegistryRecord>		record = CreateObject<ServiceRegistryRecord>(
				service,
				serviceAddress,
				nodeId);

		s_serviceRecords.insert(
				make_pair(
						service->GetServiceId(),
						record));

		s_contractRecords.insert(
				make_pair(
						service->GetContractId(),
						record));
	}

	static set<Ptr<ServiceRegistryRecord> > GetServiceRecords (uint32_t contractId)
	{
		NS_ASSERT(contractId > 0);

		//ServiceRegistry::WriteOut();

		set<Ptr<ServiceRegistryRecord> >  								records;
		Ptr<ServiceRegistryRecord> 										record;
		multimap <uint32_t, Ptr<ServiceRegistryRecord> >::iterator		it;
		pair<
			multimap<uint32_t, Ptr<ServiceRegistryRecord> >::iterator,
			multimap<uint32_t, Ptr<ServiceRegistryRecord> >::iterator> 	it_pair;

		it_pair = s_contractRecords.equal_range(contractId);

		for (it = it_pair.first; it != it_pair.second; it++ )
		{
			record = it->second;
			records.insert(record);
		}

		return records;
	}

	static Ptr<ServiceRegistryRecord> SelectDestinationService(
			Ptr<Node> srcNode,
			uint32_t destContractId)
	{
		NS_ASSERT(s_serviceSelector != NULL);

		set<Ptr<ServiceRegistryRecord> >				records;


		records = GetServiceRecords(destContractId);
		NS_ASSERT(records.size() != 0);

		return s_serviceSelector->SelectService(srcNode, destContractId, records);
	}

	static void WriteOut()
	{
		map <uint32_t, Ptr<ServiceRegistryRecord> >::iterator 			rit;
		Ptr<ServiceRegistryRecord>										record;


		NS_LOG_UNCOND("Service registry state ...");
		NS_LOG_UNCOND("	# of records: " << s_serviceRecords.size());

		for (rit=s_serviceRecords.begin() ; rit != s_serviceRecords.end(); rit++ )
		{
			record = rit->second;
		 	NS_LOG_UNCOND("Service: " << record->GetService()->GetServiceId()
					<< ", contract: " << record->GetService()->GetContractId()
					<< ", node: " << record->GetNodeId()
					<< ", address: " << record->GetServiceAddress());
		}

		NS_LOG_UNCOND("End of service registry state");
	}

}; // ServiceRegistry

map<uint32_t, Ptr<ServiceRegistryRecord> > 				ServiceRegistry::s_serviceRecords;
multimap<uint32_t, Ptr<ServiceRegistryRecord> > 		ServiceRegistry::s_contractRecords;
Ptr<ServiceRegistryServiceSelector>						ServiceRegistry::s_serviceSelector;


class ExecutionPlanExecuter : public Object, public InstanceCounter
{
private:
	const Ptr<Node> 					m_node;
	const Ptr<Message>					m_conversationMsg;
	const Ptr<ExecutionPlan>			m_plan;
	Ptr<ClientMessageEndpoint>			m_clientEndpoint;
	EventId								m_executeTaskEvent;

protected:
	const Ptr<ServiceBase>				m_serviceBase;
	const Ptr<SimulationOutput> 		m_simulationOutput;

public:

	ExecutionPlanExecuter (
			Ptr<Node> node,
			Ptr<ServiceBase> serviceBase,
			Ptr<Message> conversationMsg,
			Ptr<SimulationOutput> simulationOutput,
			Ptr<ExecutionPlan> plan)
			:InstanceCounter(typeid(this).name()),
			 m_node(node),
			 m_conversationMsg(conversationMsg),
			 m_plan (plan),
			 m_serviceBase(serviceBase),
			 m_simulationOutput(simulationOutput)
	{
		// conversationMsg - can be null
		NS_ASSERT(node != NULL);
		NS_ASSERT(serviceBase != 0);
		NS_ASSERT(simulationOutput != NULL);
		NS_ASSERT(plan != NULL);
	}

	virtual ~ExecutionPlanExecuter()
	{
		Stop();
	}

	void Start ()
	{
		NS_ASSERT(m_clientEndpoint == NULL);

		m_clientEndpoint = MessageEndpointFactory::CreateClientMessageEndpoint(
				m_node,
				m_serviceBase,
				m_simulationOutput,
				MakeCallback(&ExecutionPlanExecuter::Request_onSendSuccessCallback, this),
				MakeCallback(&ExecutionPlanExecuter::Request_onSendFailureCallback, this),
				MakeCallback(&ExecutionPlanExecuter::Request_onReceiveResponseCallback, this),
				MakeCallback(&ExecutionPlanExecuter::Request_onResponseTimeoutCallback, this));

		m_clientEndpoint->Open();

		OnStart();
	}

	void Stop ()
	{
		m_executeTaskEvent.Cancel();

		if(m_clientEndpoint != NULL)
		{
			m_clientEndpoint->Close();
			m_clientEndpoint = NULL;
		}
	}

protected:

	virtual void OnStart() = 0;
	virtual void ExecuteNextStep () = 0;
	virtual void Request_onSendSuccessCallback() = 0;
	virtual void Request_onSendFailureCallback() = 0;
	virtual void Request_onReceiveResponseCallback(Ptr<Message> msg) = 0;
	virtual void Request_onResponseTimeoutCallback() = 0;

	void ExecuteNextStepWithDelay (RandomVariable delay)
	{
		Time			delayValue = MilliSeconds(delay.GetInteger());

		m_executeTaskEvent = Simulator::Schedule (delayValue, &ExecutionPlanExecuter::ExecuteNextStep, this);
	}

	void ExecuteSendMessage (uint32_t index)
	{
		NS_ASSERT(index < m_plan->GetExecutionStepsCount());

		const Ptr<ExecutionStep> 		executionStep = m_plan->GetExecutionStep(index);
		uint32_t 						contractId = executionStep->GetContractId();
		uint32_t 						contractMethodId = executionStep->GetContractMethodId();
		Ptr<ServiceRegistryRecord> 		registryRecord = FindRequestDestination(contractId);
		uint32_t						size = executionStep->GetRequestSize().GetInteger();


		SendMessage(
				registryRecord->GetNodeId(),
				registryRecord->GetService()->GetServiceId(),
				registryRecord->GetServiceAddress(),
				contractMethodId,
				size);
	}

private:

	void SendMessage(uint32_t destNode, uint32_t destService, Address to, uint32_t destMethod, uint32_t size)
	{
		Ptr<Message> 	msg = CreateObject<Message>();


		// new conversation
		if (m_conversationMsg == NULL)
		{
			msg->InitializeNew(
							m_node->GetId(),
							m_serviceBase->GetServiceId(),
							destNode,
							destService,
							destMethod,
							size);
		}
		else // continue conversation
		{
			msg->InitializeNext (
					m_conversationMsg,
					destNode,
					destService,
					destMethod,
					size);
		}

		m_clientEndpoint->SendMessage(msg, to, true);
	}

	Ptr<ServiceRegistryRecord> FindRequestDestination(uint32_t contractId)
	{
		return ServiceRegistry::SelectDestinationService(
				m_node,
				contractId);
	}

}; // ExecutionPlanExecuter


class ServiceExecutionPlanExecuter : public ExecutionPlanExecuter
{
private:
	const Ptr<ServiceExecutionPlan>		m_servicePlan;
	int		 							m_currentStep;
	Callback<void, bool> 				m_onExecutionStop;
	const RandomVariable				m_stepSelector;
	EventId								m_finishedWithErrorDelay;


public:

	ServiceExecutionPlanExecuter (
			Ptr<Node> node,
			Ptr<ServiceBase> serviceBase,
			Ptr<Message> conversationMsg,
			Ptr<SimulationOutput> simulationOutput,
			Ptr<ServiceExecutionPlan> servicePlan,
			Callback<void, bool> onExecutionStop)
			:ExecutionPlanExecuter(
					node,
					serviceBase,
					conversationMsg,
					simulationOutput,
					servicePlan),
			 m_servicePlan(servicePlan),
			 m_onExecutionStop(onExecutionStop),
			 m_stepSelector (UniformVariable(0, 100))
	{
		NS_ASSERT(servicePlan != NULL);
		NS_ASSERT(!onExecutionStop.IsNull());
	}

	virtual ~ServiceExecutionPlanExecuter()
	{
		Stop();
		m_finishedWithErrorDelay.Cancel();
	}

protected:

	virtual void OnStart()
	{
		m_currentStep = -1;
		ExecuteNextStep();
	}

	virtual void ExecuteNextStep ()
	{
		int stepsCount = (int)m_servicePlan->GetExecutionStepsCount();


		// first step - pre exe delay
		if (m_currentStep == -1)
		{
			ExecuteNextStepWithDelay (m_servicePlan->GetPlanPreExeDelay());
			m_currentStep++;
			return;
		}

		// send request step(s)
		if (m_currentStep < stepsCount)
		{
			// find step to execute - based on configured probability of steps
			m_currentStep = FindStepToExecute();

			// step found
			if (m_currentStep < stepsCount)
			{
				ExecuteSendMessage(m_currentStep);
				m_currentStep++;
				return;
			}
		}

		// last step - post exe delay
		if (m_currentStep == stepsCount)
		{
			ExecuteNextStepWithDelay (m_servicePlan->GetPlanPostExeDelay());
			m_currentStep++;
			return;
		}

		// end of execution
		if (m_currentStep > stepsCount)
		{
			PlanFinished(true);
			return;
		}
	}

	virtual void Request_onSendSuccessCallback()
	{}

	virtual void Request_onSendFailureCallback()
	{
		ExecutePlanFinishedWithErrorDelay();
	}

	virtual void Request_onReceiveResponseCallback(Ptr<Message> msg)
	{
		NS_ASSERT(msg != NULL);

		// delay of the step
		const RandomVariable 		delay 			= m_servicePlan->GetStepPostExeDelay();


		// check for exception - if yes cancel the task
		if (msg->GetMessageType() == Message::MTResponseException)
		{
			m_simulationOutput->RecordError(m_serviceBase->GetServiceId(), ERROR_TYPE_RECEIVED_EXCEPTION, msg);
			ExecutePlanFinishedWithErrorDelay();
		}
		else
		{
			ExecuteNextStepWithDelay(delay);
		}
	}

	virtual void Request_onResponseTimeoutCallback()
	{
		ExecutePlanFinishedWithErrorDelay();
	}

	void ExecutePlanFinishedWithErrorDelay()
	{
		Time			delayValue = MilliSeconds(m_servicePlan->GetPostPlanErrorDelay().GetInteger());


		m_finishedWithErrorDelay = Simulator::Schedule (
				delayValue,
				&ServiceExecutionPlanExecuter::PlanFinished,
				this,
				false);
	}

	void PlanFinished(bool success)
	{
		Stop();
		m_onExecutionStop(success);
	}

	uint32_t FindStepToExecute()
	{
		double 			stepProbability;
		uint32_t		step;
		uint32_t		stepsCount = m_servicePlan->GetExecutionStepsCount();


		step = m_currentStep;

		while (step < stepsCount)
		{
			stepProbability = m_servicePlan->GetExecutionStep(step)->GetStepProbability();

			//NS_LOG_UNCOND("step selection - step: " << step << " p:" << stepProbability);

			if (m_stepSelector.GetValue () <= stepProbability)
			{
				//NS_LOG_UNCOND("selected");
				return step;
			}

			step++;
		}

		//NS_LOG_UNCOND("no step selected");
		return stepsCount;
	}

}; // ServiceExecutionPlanExecuter




class ClientExecutionPlanExecuter : public ExecutionPlanExecuter
{
private:
	const Ptr<ClientExecutionPlan>				m_clientPlan;
	const RandomVariable						m_stepSelector;
	const RandomVariable						m_stepProbabilitySelector;
	uint32_t									m_latestStep;

public:

	ClientExecutionPlanExecuter (
			Ptr<Node> node,
			Ptr<ServiceBase> serviceBase,
			Ptr<Message> conversationMsg,
			Ptr<SimulationOutput> simulationOutput,
			Ptr<ClientExecutionPlan> clientPlan)
			:ExecutionPlanExecuter(
					node,
					serviceBase,
					conversationMsg,
					simulationOutput,
					clientPlan),
			m_clientPlan (clientPlan),
			m_stepSelector (UniformVariable(0, m_clientPlan->GetExecutionStepsCount())),
			m_stepProbabilitySelector (UniformVariable(0, 100))
	{
		NS_ASSERT(clientPlan != NULL);
	}

	virtual ~ClientExecutionPlanExecuter()
	{
		Stop();
	}

protected:

	virtual void OnStart()
	{
		WaitBeforeNextStep();
	}

	virtual void ExecuteNextStep ()
	{
		uint32_t step = FindNextStepToExecute();

		//NS_LOG_UNCOND("client: ExecuteNextStep clientid " << m_serviceBase->GetServiceId() << " stepid: " << step);

		ExecuteSendMessage(step);
	}

	uint32_t FindNextStepToExecute()
	{
		double 					stepProbability;
		uint32_t				step;


		while (true)
		{
			step = m_stepSelector.GetInteger();

			stepProbability = m_clientPlan->GetExecutionStep(step)->GetStepProbability();

			if (m_stepProbabilitySelector.GetValue () <= stepProbability)
			{
				return step;
			}
		}
	}

	void WaitBeforeNextStep()
	{
		ExecuteNextStepWithDelay(m_clientPlan->GetRequestRate());
	}

	void WaitAfterFailure()
	{
		//NS_LOG_UNCOND("client " << m_serviceBase->GetServiceId() << " WaitAfterFailure");
		ExecuteNextStepWithDelay(m_clientPlan->GetAfterFailureWaitingPeriod());
	}

	virtual void Request_onSendSuccessCallback()
	{}

	virtual void Request_onSendFailureCallback()
	{
		WaitBeforeNextStep();
	}

	virtual void Request_onReceiveResponseCallback(Ptr<Message> msg)
	{
		if (msg->GetMessageType() == Message::MTResponseException)
		{
			m_simulationOutput->RecordError(m_serviceBase->GetServiceId(), ERROR_TYPE_RECEIVED_EXCEPTION, msg);
			WaitAfterFailure();
		}
		else
		{
			WaitBeforeNextStep();
		}
	}

	virtual void Request_onResponseTimeoutCallback()
	{
		//WaitBeforeNextStep();
		WaitAfterFailure();
	}

}; // ClientExecutionPlanExecuter



class ServiceRequestTask;

class ServiceTaskManager : public Object, public InstanceCounter
{
private:
	set<Ptr<ServiceRequestTask> > 			m_runningTasks;

public:

	ServiceTaskManager ()
	:InstanceCounter(typeid(this).name())
	{}

	virtual ~ServiceTaskManager()
	{
		NS_ASSERT(m_runningTasks.size() == 0);
	}

	void AddTask(Ptr<ServiceRequestTask> task)
	{
		NS_ASSERT(task);

		m_runningTasks.insert(task);
	}

	void RemoveTask (Ptr<ServiceRequestTask> task)
	{
		NS_ASSERT(task);

		m_runningTasks.erase(task);
	}

	void StopAllTasks()
	{
		set<Ptr<ServiceRequestTask> >::iterator 		it;
		Ptr<ServiceRequestTask> 						task;


		for (it=m_runningTasks.begin(); it!=m_runningTasks.end(); it++)
		{
			task = *it;
			//task->Stop(); !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
		}

		NS_ASSERT(m_runningTasks.size() == 0);
	}

}; // ServiceTaskManager


class ServiceRequestTask : public Object, public InstanceCounter
{
private:
	const Ptr<Node> 					m_node;
	const Ptr<Service> 					m_service;
	const Ptr<Message> 					m_conversationMsg;
	const Address 						m_requestAddress;
	Ptr<ServiceMethod> 					m_requestMethod;
	const Ptr<ServiceTaskManager> 		m_taskManager;
	const Ptr<SimulationOutput> 		m_simulationOutput;
	Ptr<ExecutionPlanExecuter>			m_planExecuter;
	Ptr<ClientMessageEndpoint> 			m_responseEndpoint;
	EventId								m_errorStopEvent;

	static uint32_t						s_numberOfStartedMethods;
	static uint32_t						s_numberOfFailedMethods;
	static uint32_t						s_numberOfFailedExecutions;
	static uint32_t						s_numberOfServiceFailures;
	static uint32_t						s_numberOfIssuedExceptionMessages;

public:

	ServiceRequestTask (
			Ptr<Node> node,
			Ptr<Service> service,
			Ptr<Message> conversationMsg,
			Address requestAddress,
			Ptr<ServiceTaskManager> taskManager,
			Ptr<SimulationOutput> simulationOutput)
		:InstanceCounter(typeid(this).name()),
		 m_node(node),
		 m_service (service),
		m_conversationMsg (conversationMsg),
		m_requestAddress (requestAddress),
		m_taskManager (taskManager),
		m_simulationOutput (simulationOutput)
	{
		NS_ASSERT(service != NULL);
		NS_ASSERT(node != NULL);
		NS_ASSERT(conversationMsg != NULL);
		NS_ASSERT(taskManager != NULL);
		NS_ASSERT(simulationOutput != NULL);

		m_requestMethod = GetRequestMethod();
		NS_ASSERT(m_requestMethod != NULL);

		m_planExecuter = CreateObject<ServiceExecutionPlanExecuter>(
				node,
				m_service,
				m_conversationMsg,
				m_simulationOutput,
				m_requestMethod->GetExecutionPlan(),
				MakeCallback(&ServiceRequestTask::OnExecutionStopCallback, this));
	}

	virtual ~ServiceRequestTask()
	{
		Stop();
		m_errorStopEvent.Cancel();
	}

	void Start ()
	{
		bool isGeneratingException = false;


		// check if there is service error - if yes send exception (if required)
		if (IsServiceProcessingError(isGeneratingException))
		{
			m_simulationOutput->RecordError(m_service->GetServiceId(), ERROR_TYPE_SERVICE_PROCESSING, m_conversationMsg);
			s_numberOfServiceFailures++;
			ExecutionStopWithErrorDelay(isGeneratingException);
			return;
		}

		s_numberOfStartedMethods++;

		// check if there is method error - if yes send exception
		if (IsMethodProcessingError(isGeneratingException))
		{
			m_simulationOutput->RecordError(m_service->GetServiceId(), ERROR_TYPE_METHOD_PROCESSING, m_conversationMsg);
			s_numberOfFailedMethods++;
			ExecutionStopWithErrorDelay(isGeneratingException);
			return;
		}

		m_planExecuter->Start();
	}

	void Stop ()
	{
		m_planExecuter->Stop();
		StopServiceRequestTask();
	}

	static uint32_t GetNumberOfStartedMethods () { return s_numberOfStartedMethods; }
	static uint32_t GetNumberOfFailedMethods () {return s_numberOfFailedMethods; }
	static uint32_t GetNumberOfFailedExecutions () {return s_numberOfFailedExecutions; }
	static uint32_t GetNumberOfServiceFailures () {return s_numberOfServiceFailures; }
	static uint32_t GetNumberOfIssuedExceptionMessages () {return s_numberOfIssuedExceptionMessages; }

private:

	bool IsMethodProcessingError(bool & isGeneratingException)
	{
		return m_requestMethod->GetFaultModel()->IsCorrupt(isGeneratingException);
	}

	bool IsServiceProcessingError(bool & isGeneratingException)
	{
		return m_service->GetFaultModel()->IsCorrupt(isGeneratingException);
	}

	void ExecutionStopWithErrorDelay (bool isGeneratingException)
	{
		Time			delayValue = MilliSeconds(m_service->GetPostErrorDelay().GetInteger());


		m_errorStopEvent = Simulator::Schedule (
				delayValue,
				&ServiceRequestTask::OnExecutionStop,
				this,
				false,
				isGeneratingException);
	}

	void OnExecutionStopCallback(bool success)
	{
		// propagation of exceptions - all what comes from plan - socket faults, channel faults, received exceptions, timeouts
		OnExecutionStop(success, true);
	}

	void OnExecutionStop(bool success, bool isGeneratingException)
	{
		Ptr<Message> 					msg = CreateObject<Message> ();
		uint32_t						size = m_requestMethod->GetResponseSize().GetInteger();


		if (success)
		{
			msg->InitializeResponse (m_conversationMsg, size);
		}
		else
		{
			s_numberOfFailedExecutions++;

			if (isGeneratingException)
			{
				msg->InitializeResponseException(m_conversationMsg);
				s_numberOfIssuedExceptionMessages++;
			}
		}

		if (success || isGeneratingException)
		{
			m_responseEndpoint = MessageEndpointFactory::CreateClientMessageEndpoint(
					m_node,
					m_service,
					m_simulationOutput,
					MakeCallback(&ServiceRequestTask::Response_onSendSuccessCallback, this),
					MakeCallback(&ServiceRequestTask::Response_onSendFailureCallback, this),
					MakeCallback(&ServiceRequestTask::Response_onReceiveResponseCallback, this),
					MakeCallback(&ServiceRequestTask::Response_onResponseTimeoutCallback, this));

			m_responseEndpoint->Open();
			m_responseEndpoint->SendMessage(msg, m_requestAddress, false);
		}
	}

	void StopServiceRequestTask()
	{
		if (m_responseEndpoint != NULL)
		{
			m_responseEndpoint->Close();
			m_responseEndpoint = NULL;
		}
	}

	Ptr<ServiceMethod> GetRequestMethod()
	{
		uint32_t methodId = m_conversationMsg->GetDestMethod();
		Ptr<ServiceMethod> method = m_service->GetMethod (methodId);

		return method;
	}

	// send processing response back to requester - callbacks from endpoint
	void Response_onSendSuccessCallback()
	{
		StopServiceRequestTask();
	}

	void Response_onSendFailureCallback()
	{
		StopServiceRequestTask();
	}

	void Response_onReceiveResponseCallback(Ptr<Message> msg)
	{}

	void Response_onResponseTimeoutCallback()
	{}

}; // ServiceRequestTask

uint32_t ServiceRequestTask::s_numberOfStartedMethods = 0;
uint32_t ServiceRequestTask::s_numberOfFailedMethods = 0;
uint32_t ServiceRequestTask::s_numberOfFailedExecutions = 0;
uint32_t ServiceRequestTask::s_numberOfServiceFailures = 0;
uint32_t ServiceRequestTask::s_numberOfIssuedExceptionMessages = 0;


class ServiceInstance : public Application, public InstanceCounter
{
private:
	const Ptr<Service>					m_service;
	const uint16_t						m_receivePort;
	const Ptr<SimulationOutput> 		m_simulationOutput;
	Ptr<ServerMessageEndpoint>			m_serverEndpoint;
	Ptr<ServiceTaskManager> 			m_taskManager;

	static uint32_t						s_numberOfServiceRequests;

public:

	ServiceInstance (Ptr<Service> service, uint16_t receivePort, const Ptr<SimulationOutput> simulationOutput)
		:InstanceCounter(typeid(this).name()),
		 m_service (service),
		 m_receivePort (receivePort),
		 m_simulationOutput (simulationOutput)
	{
		NS_ASSERT(service != NULL);
		NS_ASSERT(receivePort > 0);
		NS_ASSERT(simulationOutput != NULL);

		m_taskManager = CreateObject<ServiceTaskManager>();
	}

	virtual ~ServiceInstance() {}

	static uint32_t GetNumberOfServiceRequests () { return s_numberOfServiceRequests; }
	//Ptr<Service> GetService() { return m_service; }

private:

	virtual void StartApplication (void)
	{
		m_serverEndpoint = MessageEndpointFactory::CreateServerMessageEndpoint(
				GetNode(),
				m_service,
				m_simulationOutput,
				MakeCallback(&ServiceInstance::OnReceiveRequest, this),
				m_receivePort);

		m_serverEndpoint->Open();

		ServiceRegistry::RegisterService (
				m_service,
				m_serverEndpoint->GetServerSocketAddress(),
				GetNode()->GetId());
	}

	virtual void StopApplication (void)
	{
		m_taskManager->StopAllTasks();
		m_serverEndpoint->Close();
	}

	void OnReceiveRequest (Ptr<Message> msg, Address from)
	{
		NS_ASSERT(msg != NULL);

		Ptr<ServiceRequestTask> task;


		s_numberOfServiceRequests++;

		task = CreateObject<ServiceRequestTask>(
				GetNode(),
				m_service,
				msg,
				from,
				m_taskManager,
				m_simulationOutput);

		m_taskManager->AddTask(task);

		task->Start();
	}

}; // ServiceInstance

uint32_t ServiceInstance::s_numberOfServiceRequests = 0;


class ClientInstance : public Application, public InstanceCounter
{
private:
	const Ptr<Client>				m_client;
	const Ptr<SimulationOutput> 	m_simulationOutput;
	Ptr<ExecutionPlanExecuter>		m_planExecuter;

public:

	ClientInstance (Ptr<Client> client, Ptr<SimulationOutput> simulationOutput)
		:InstanceCounter(typeid(this).name()),
		 m_client (client),
		 m_simulationOutput(simulationOutput)
	{
		NS_ASSERT(client != NULL);
		NS_ASSERT(simulationOutput != NULL);
	}

	virtual ~ClientInstance() {}

private:

	virtual void StartApplication (void)
	{
		Ptr<ClientExecutionPlan> 			clientExecutionPlan = DynamicCast<ClientExecutionPlan>(m_client->GetExecutionPlan());


		m_planExecuter = CreateObject<ClientExecutionPlanExecuter>(
								GetNode(),
								m_client,
								Ptr<Message>(NULL),
								m_simulationOutput,
								clientExecutionPlan);

		m_planExecuter->Start();
	}

	virtual void StopApplication (void)
	{
		if (m_planExecuter != NULL)
		{
			m_planExecuter->Stop();
			m_planExecuter = NULL;
		}
	}

}; // ClientInstance


class ServiceConfigurationRandomGenerator
{
private:
	Ptr<ServiceConfiguration>				m_serviceConfiguration;
	map<uint32_t, Ptr<ServiceMethod> >		m_serviceMethods;

public:

	ServiceConfigurationRandomGenerator ()
	{
		m_serviceConfiguration = CreateObject<ServiceConfiguration>();
	}

	virtual ~ServiceConfigurationRandomGenerator () {}

	Ptr<ServiceConfiguration> GetServiceConfiguration () const { return m_serviceConfiguration; }

	void GenerateServices (
			uint32_t numberOfServices,
			uint32_t serviceBaseId,
			uint32_t contractBaseId,
			RandomVariable numberOfReplicas,
			RandomVariable startTime,
			RandomVariable stopTime,
			Time responseTimeout,
			Time ACKTimeout,
			uint32_t retransmissionLimit,
			Time msgIdLifetime,
			Ptr<FaultModel> serviceFaultModel,
			RandomVariable numberOfServiceMethods,
			RandomVariable methodResponseSize,
			Ptr<FaultModel> methodFaultModel,
			RandomVariable methodPreExeDelay,
			RandomVariable methodPostExeDelay,
			RandomVariable methodPostPlanErrorDelay,
			double executionStepDependencyProbability,
			RandomVariable executionStepPostExeDelay,
			RandomVariable executionStepRequestSize,
			RandomVariable stepProbability,
			RandomVariable servicePostErrorDelay)
	{
		NS_ASSERT(numberOfServices != 0);
		NS_ASSERT(serviceBaseId != 0);
		NS_ASSERT(contractBaseId != 0);
		NS_ASSERT(executionStepDependencyProbability > 0);
		NS_ASSERT(serviceFaultModel != NULL);
		NS_ASSERT(methodFaultModel != NULL);

		AddServices(
				numberOfServices,
				serviceBaseId,
				contractBaseId,
				startTime,
				stopTime,
				responseTimeout,
				ACKTimeout,
				retransmissionLimit,
				msgIdLifetime,
				serviceFaultModel,
				servicePostErrorDelay);

		AddServiceMethods (
					numberOfServices,
					serviceBaseId,
					numberOfServiceMethods,
					methodResponseSize,
					methodFaultModel,
					methodPreExeDelay,
					methodPostExeDelay,
					executionStepPostExeDelay,
					methodPostPlanErrorDelay);

		AddServiceExecutionSteps (
					executionStepDependencyProbability,
					executionStepPostExeDelay,
					executionStepRequestSize,
					stepProbability);

		AddServicesReplicas(
				numberOfServices,
				serviceBaseId,
				numberOfReplicas);
	}

	void GenerateClientsWithUniformDependenceProbability (
			bool deployClientsRandomly,
			uint32_t numberOfClients,
			uint32_t clientBaseId,
			RandomVariable startTime,
			RandomVariable stopTime,
			Time responseTimeout,
			Time ACKTimeout,
			uint32_t retransmissionLimit,
			Time msgIdLifetime,
			RandomVariable planRequestRate,
			double executionStepDependencyProbability,
			RandomVariable executionStepRequestSize,
			RandomVariable afterFailureWaitingPeriod)
	{
		NS_ASSERT(numberOfClients != 0);
		NS_ASSERT(clientBaseId != 0);
		NS_ASSERT(executionStepDependencyProbability > 0);
		NS_ASSERT(retransmissionLimit != 0);

		m_serviceConfiguration->SetDeployClientsRandomly(deployClientsRandomly);

		AddClients(
					numberOfClients,
					clientBaseId,
					startTime,
					stopTime,
					responseTimeout,
					ACKTimeout,
					retransmissionLimit,
					msgIdLifetime,
					planRequestRate,
					afterFailureWaitingPeriod);

		AddClientExecutionStepsWithUniformDependenceProbability (
				executionStepDependencyProbability,
				executionStepRequestSize);
	}

	void GenerateClientsWithRandomFixedDependenceProbabilityToAllMethods (
			bool deployClientsRandomly,
			uint32_t numberOfClients,
			uint32_t clientBaseId,
			RandomVariable startTime,
			RandomVariable stopTime,
			Time responseTimeout,
			Time ACKTimeout,
			uint32_t retransmissionLimit,
			Time msgIdLifetime,
			RandomVariable planRequestRate,
			RandomVariable executionStepRequestSize,
			RandomVariable afterFailureWaitingPeriod,
			RandomVariable stepProbability)
	{
		NS_ASSERT(numberOfClients != 0);
		NS_ASSERT(clientBaseId != 0);
		NS_ASSERT(retransmissionLimit != 0);

		m_serviceConfiguration->SetDeployClientsRandomly(deployClientsRandomly);

		AddClients(
					numberOfClients,
					clientBaseId,
					startTime,
					stopTime,
					responseTimeout,
					ACKTimeout,
					retransmissionLimit,
					msgIdLifetime,
					planRequestRate,
					afterFailureWaitingPeriod);

		AddClientExecutionStepsWithRandomFixedDependenceProbabilityToAllMethods (
				executionStepRequestSize,
				stepProbability);
	}

	void GenerateClientsWithDecreasingDependenceProbabilityToAllServices (
			bool deployClientsRandomly,
			uint32_t numberOfClients,
			uint32_t clientBaseId,
			RandomVariable startTime,
			RandomVariable stopTime,
			Time responseTimeout,
			Time ACKTimeout,
			uint32_t retransmissionLimit,
			Time msgIdLifetime,
			RandomVariable planRequestRate,
			RandomVariable executionStepRequestSize,
			RandomVariable afterFailureWaitingPeriod)
	{
		NS_ASSERT(numberOfClients != 0);
		NS_ASSERT(clientBaseId != 0);
		NS_ASSERT(retransmissionLimit != 0);

		double 		servicesDependenceProbabilities [m_serviceConfiguration->GetServices().size()];


		m_serviceConfiguration->SetDeployClientsRandomly(deployClientsRandomly);

		AddClients(
					numberOfClients,
					clientBaseId,
					startTime,
					stopTime,
					responseTimeout,
					ACKTimeout,
					retransmissionLimit,
					msgIdLifetime,
					planRequestRate,
					afterFailureWaitingPeriod);


		GenerateServiceDependenceProbabilitiesForScenario_DecreasingDependenceProbabilityToAllServices (
			servicesDependenceProbabilities);

		AddClientExecutionStepsWithDecreasingDependenceProbabilityToAllServices (
			executionStepRequestSize,
			servicesDependenceProbabilities);
	}



	void GenerateClientsWithRandomFixedDependenceProbabilityToNServices (
			bool deployClientsRandomly,
			uint32_t numberOfClients,
			uint32_t clientBaseId,
			RandomVariable startTime,
			RandomVariable stopTime,
			Time responseTimeout,
			Time ACKTimeout,
			uint32_t retransmissionLimit,
			Time msgIdLifetime,
			RandomVariable planRequestRate,
			RandomVariable executionStepRequestSize,
			RandomVariable afterFailureWaitingPeriod,
			uint32_t numberOfServicesToBeUsedByClients,
			RandomVariable stepProbability)
	{
		NS_ASSERT(numberOfClients != 0);
		NS_ASSERT(clientBaseId != 0);
		NS_ASSERT(retransmissionLimit != 0);

		uint32_t servicesToBeUsedByClients [numberOfServicesToBeUsedByClients];


		m_serviceConfiguration->SetDeployClientsRandomly(deployClientsRandomly);

		AddClients(
					numberOfClients,
					clientBaseId,
					startTime,
					stopTime,
					responseTimeout,
					ACKTimeout,
					retransmissionLimit,
					msgIdLifetime,
					planRequestRate,
					afterFailureWaitingPeriod);

		SelectServicesToBeUsedByClients (
					numberOfServicesToBeUsedByClients,
					servicesToBeUsedByClients);

		AddClientExecutionStepsWithRandomFixedDependenceProbabilityToNServices (
				executionStepRequestSize,
				stepProbability,
				servicesToBeUsedByClients,
				numberOfServicesToBeUsedByClients);
	}

	void GenerateClientsWithFrontEndBackEndServices (
			bool deployClientsRandomly,
			uint32_t numberOfClients,
			uint32_t clientBaseId,
			RandomVariable startTime,
			RandomVariable stopTime,
			Time responseTimeout,
			Time ACKTimeout,
			uint32_t retransmissionLimit,
			Time msgIdLifetime,
			RandomVariable planRequestRate,
			RandomVariable executionStepRequestSize,
			RandomVariable afterFailureWaitingPeriod,
			uint32_t frontEndServices [],
			uint32_t numberOfFrontEndServices,
			RandomVariable stepProbability)
	{
		NS_ASSERT(numberOfClients != 0);
		NS_ASSERT(clientBaseId != 0);
		NS_ASSERT(retransmissionLimit != 0);

		m_serviceConfiguration->SetDeployClientsRandomly(deployClientsRandomly);

		AddClients(
					numberOfClients,
					clientBaseId,
					startTime,
					stopTime,
					responseTimeout,
					ACKTimeout,
					retransmissionLimit,
					msgIdLifetime,
					planRequestRate,
					afterFailureWaitingPeriod);

		AddClientExecutionStepsWithRandomFixedDependenceProbabilityToNServices (
				executionStepRequestSize,
				stepProbability,
				frontEndServices,
				numberOfFrontEndServices);
	}

	void GenerateClientsWithSingleServiceDependence (
			bool deployClientsRandomly,
			uint32_t numberOfClients,
			uint32_t clientBaseId,
			RandomVariable startTime,
			RandomVariable stopTime,
			Time responseTimeout,
			Time ACKTimeout,
			uint32_t retransmissionLimit,
			Time msgIdLifetime,
			RandomVariable planRequestRate,
			RandomVariable executionStepRequestSize,
			RandomVariable afterFailureWaitingPeriod,
			uint32_t singleServiceId)
	{
		NS_ASSERT(numberOfClients != 0);
		NS_ASSERT(clientBaseId != 0);
		NS_ASSERT(retransmissionLimit != 0);

		uint32_t servicesToBeUsedByClients [1];
		RandomVariable stepProbability = ConstantVariable(100);


		m_serviceConfiguration->SetDeployClientsRandomly(deployClientsRandomly);

		AddClients(
					numberOfClients,
					clientBaseId,
					startTime,
					stopTime,
					responseTimeout,
					ACKTimeout,
					retransmissionLimit,
					msgIdLifetime,
					planRequestRate,
					afterFailureWaitingPeriod);

		servicesToBeUsedByClients[0] = singleServiceId;

		AddClientExecutionStepsWithRandomFixedDependenceProbabilityToNServices (
				executionStepRequestSize,
				stepProbability,
				servicesToBeUsedByClients,
				1); // uint32_t numberOfServicesToBeUsedByClients
	}

private:


	void SelectServicesToBeUsedByClients (
			uint32_t numberOfServicesToBeUsedByClients,
			uint32_t servicesToBeUsedByClients [])
	{
		/*
		uint32_t			numberOfServices = m_serviceConfiguration->GetServices().size();
		RandomVariable		serviceSelector = UniformVariable(0, numberOfServices - 1);


		for(uint32_t i = 0; i<numberOfServicesToBeUsedByClients; i++)
		{
			servicesToBeUsedByClients[i] = serviceSelector.GetInteger();
		}
		 */
		// this has to be equaly distributed over the whole number space
		// using first N random numbers misses 0 to 10 completly which has significat impact on the results
		// thus split the number space to bins and then randomly select in each bin the services

		uint32_t			numberOfServices = m_serviceConfiguration->GetServices().size();
		uint32_t			sizeOfBin = 6;
		uint32_t			numberOfBins = numberOfServices / sizeOfBin;
		uint32_t			numberOfServicesToSelectInBin = numberOfServicesToBeUsedByClients / numberOfBins;
		RandomVariable		serviceSelector = UniformVariable(0, sizeOfBin - 1);
		uint32_t			i = 0;
		uint32_t			sid;

		for(uint32_t b = 0; b<numberOfBins; b++)
		{
			for(uint32_t s = 0; s<numberOfServicesToSelectInBin; s++)
			{
				sid = (b * sizeOfBin) + serviceSelector.GetInteger();
				servicesToBeUsedByClients[i] = sid;
				//NS_LOG_UNCOND(sid);
				i++;
			}
		}
	}

	bool ShouldDependencyBeCreated(double dependencyProbability)
	{
		NS_ASSERT(dependencyProbability > 0.);

		static RandomVariable   randomVariable = UniformVariable (0.0, 1.0);

		return (randomVariable.GetValue () < dependencyProbability);
	}

	void AddServices(
			uint32_t numberOfServices,
			uint32_t serviceBaseId,
			uint32_t contractBaseId,
			RandomVariable startTime,
			RandomVariable stopTime,
			Time responseTimeout,
			Time ACKTimeout,
			uint32_t retransmissionLimit,
			Time msgIdLifetime,
			Ptr<FaultModel> serviceFaultModel,
			RandomVariable postErrorDelay)
	{
		NS_ASSERT(numberOfServices != 0);
		NS_ASSERT(serviceBaseId != 0);
		NS_ASSERT(contractBaseId != 0);
		NS_ASSERT(serviceFaultModel != NULL);

		uint32_t 		serviceId = serviceBaseId;
		uint32_t 		contractId = contractBaseId;
		Time			startTimeValue;
		Time			stopTimeValue;


		for (uint32_t i = 0; i < numberOfServices; i++)
		{
			startTimeValue = MilliSeconds(startTime.GetInteger());
			stopTimeValue = MilliSeconds(stopTime.GetInteger());

			m_serviceConfiguration->AddService(
					serviceId,
					startTimeValue,
					stopTimeValue,
					responseTimeout,
					ACKTimeout,
					retransmissionLimit,
					msgIdLifetime,
					contractId,
					serviceFaultModel,
					postErrorDelay);

			serviceId++;
			contractId++;
		}
	}

	void AddServicesReplicas(
			uint32_t numberOfServices,
			uint32_t serviceBaseId,
			RandomVariable numberOfReplicas)
	{
		NS_ASSERT(numberOfServices != 0);
		NS_ASSERT(serviceBaseId != 0);

		uint32_t 		serviceId = serviceBaseId;
		uint32_t		newServiceId = serviceId + numberOfServices - 1;


		for (uint32_t i = 0; i < numberOfServices; i++)
		{
			newServiceId = AddServiceReplicas(
					serviceId,
					newServiceId,
					numberOfReplicas);

			serviceId++;
		}
	}

	uint32_t AddServiceReplicas(
			uint32_t serviceId,
			uint32_t newServiceId,
			RandomVariable numberOfReplicas)
	{
		NS_ASSERT(serviceId != 0);

		uint32_t				valueNumberOfReplicas = numberOfReplicas.GetInteger();
		//uint32_t				newServiceId = serviceId;


		for (uint32_t i = 0; i<valueNumberOfReplicas; i++)
		{
			newServiceId++;
			m_serviceConfiguration->AddServiceReplica(serviceId, newServiceId);
		}

		return newServiceId;
	}

	void AddServiceMethods (
			uint32_t numberOfServices,
			uint32_t serviceBaseId,
			RandomVariable numberOfServiceMethods,
			RandomVariable responseSize,
			Ptr<FaultModel> methodFaultModel,
			RandomVariable planPreExeDelay,
			RandomVariable planPostExeDelay,
			RandomVariable stepPostExeDelay,
			RandomVariable postPlanErrorDelay)
	{
		NS_ASSERT(methodFaultModel != NULL);

		uint32_t 				serviceId = 0;
		uint32_t				actualNumberOfServiceMethods;
		uint32_t				methodId = 0;
		Ptr<ServiceMethod>		serviceMethod;


		// for each service rather the ids
		for (uint32_t s = 0; s < numberOfServices; s++)
		{
			serviceId = serviceBaseId + s;
			actualNumberOfServiceMethods = numberOfServiceMethods.GetInteger();

			for (uint32_t m = 0; m < actualNumberOfServiceMethods; m++)
			{
				methodId++;
				serviceMethod = m_serviceConfiguration->AddServiceMethod(
						serviceId,
						methodId,
						responseSize,
						methodFaultModel,
						planPreExeDelay,
						planPostExeDelay,
						stepPostExeDelay,
						postPlanErrorDelay);

				m_serviceMethods.insert(pair<uint32_t, Ptr<ServiceMethod> >(methodId, serviceMethod));
			}
		}
	}

	/*
	 * Dependency generation - generates random graph of services' dependencies with probability p (any random variable)
	 * for each service method
	 * goes for each other method (excluding same contract) and queries if the dependency should be established
	 * */
	void AddServiceExecutionSteps(
			double dependencyProbability,
			RandomVariable postExeDelay,
			RandomVariable requestSize,
			RandomVariable stepProbability)
	{
		NS_ASSERT(dependencyProbability > 0.);

		map<uint32_t, Ptr<ServiceMethod> >::iterator 	x;
		map<uint32_t, Ptr<ServiceMethod> >::iterator 	y;
		Ptr<ServiceMethod>								dependent;
		Ptr<ServiceMethod>								antecedent;


		for (x = m_serviceMethods.begin(); x != m_serviceMethods.end(); x++)
		{
			for (y = m_serviceMethods.begin(); y != m_serviceMethods.end(); y++)
			{
				dependent = x->second;
				antecedent = y->second;

				// no same contract dependency
				if (dependent->GetService()->GetContractId() ==
						antecedent->GetService()->GetContractId())
				{
					continue;
				}

				// service to service dependency can be only from lower id service to higher id service
				// prevents dependency circles !
				if (dependent->GetService()->GetServiceId() >=
						antecedent->GetService()->GetServiceId())
				{
					continue;
				}


				// should this link be established
				if (ShouldDependencyBeCreated(dependencyProbability))
				{
					m_serviceConfiguration->AddServiceExecutionStep(
							dependent->GetService()->GetServiceId(),
							dependent->GetContractMethodId(),
							antecedent->GetService()->GetContractId(),
							antecedent->GetContractMethodId(),
							requestSize,
							stepProbability.GetValue());
				}
			}
		}
	}

	void AddClients(
			uint32_t numberOfClients,
			uint32_t clientBaseId,
			RandomVariable startTime,
			RandomVariable stopTime,
			Time responseTimeout,
			Time ACKTimeout,
			uint32_t retransmissionLimit,
			Time msgIdLifetime,
			RandomVariable planRequestRate,
			RandomVariable afterFailureWaitingPeriod)
	{
		NS_ASSERT(numberOfClients != 0);
		NS_ASSERT(clientBaseId != 0);

		uint32_t 		clientId = 0;
		Time			startTimeValue;
		Time			stopTimeValue;


		for (uint32_t i = 0; i < numberOfClients; i++)
		{
			clientId = clientBaseId + i;
			startTimeValue = MilliSeconds(startTime.GetInteger());
			stopTimeValue = MilliSeconds(stopTime.GetInteger());

			m_serviceConfiguration->AddClient(
					clientId,
					startTimeValue,
					stopTimeValue,
					responseTimeout,
					ACKTimeout,
					retransmissionLimit,
					msgIdLifetime,
					planRequestRate,
					afterFailureWaitingPeriod);
		}
	}

	/*
	 * Dependency generation - generates part of random graph of client to service dependencies with probability p (any random variable)
	 * for each client method
	 * goes for each service method and queries if the dependency should be established
	 * */
	void AddClientExecutionStepsWithUniformDependenceProbability (
			double dependencyProbability,
			RandomVariable requestSize)
	{
		NS_ASSERT(dependencyProbability > 0.);

		const map<uint32_t, Ptr<Client> > & 				clients = m_serviceConfiguration->GetClients();
		Ptr<Client>											client;
		map<uint32_t, Ptr<Client> >::const_iterator			x;
		map<uint32_t, Ptr<ServiceMethod> >::const_iterator 	y;
		Ptr<ServiceMethod>									antecedent;
		bool 												hasClientAtLeastOneDependency;


		for (x = clients.begin(); x != clients.end(); x++)
		{
			hasClientAtLeastOneDependency = false;

			for (y = m_serviceMethods.begin(); y != m_serviceMethods.end(); y++)
			{
				client = x->second;
				antecedent = y->second;

				// should this dependency be established
				if (ShouldDependencyBeCreated(dependencyProbability))
				{
					m_serviceConfiguration->AddClientExecutionStep(
							client->GetServiceId(),
							antecedent->GetService()->GetContractId(),
							antecedent->GetContractMethodId(),
							requestSize,
							100);

					hasClientAtLeastOneDependency=true;
				}
			}

			// if client has not dependencies at all - add randomly one - otherwise the scenario will not pass
			if (!hasClientAtLeastOneDependency)
			{
				RandomVariable 	singleDependencySelector = UniformVariable(0, m_serviceMethods.size() -1);
				uint32_t			singleDependency = singleDependencySelector.GetInteger();

				antecedent = m_serviceMethods[singleDependency];

				m_serviceConfiguration->AddClientExecutionStep(
						client->GetServiceId(),
						antecedent->GetService()->GetContractId(),
						antecedent->GetContractMethodId(),
						requestSize,
						0);
			}
		}
	}

	void AddClientExecutionStepsWithRandomFixedDependenceProbabilityToAllMethods (
			RandomVariable requestSize,
			RandomVariable stepProbability)
	{
		const map<uint32_t, Ptr<Client> > & 				clients = m_serviceConfiguration->GetClients();
		Ptr<Client>											client;
		map<uint32_t, Ptr<Client> >::const_iterator			x;
		map<uint32_t, Ptr<ServiceMethod> >::const_iterator 	y;
		Ptr<ServiceMethod>									antecedent;


		for (x = clients.begin(); x != clients.end(); x++)
		{
			for (y = m_serviceMethods.begin(); y != m_serviceMethods.end(); y++)
			{
				client = x->second;
				antecedent = y->second;

				m_serviceConfiguration->AddClientExecutionStep(
						client->GetServiceId(),
						antecedent->GetService()->GetContractId(),
						antecedent->GetContractMethodId(),
						requestSize,
						stepProbability.GetValue());
			}
		}
	}

	void GenerateServiceDependenceProbabilitiesForScenario_DecreasingDependenceProbabilityToAllServices (
			double servicesDependenceProbabilities [])
	{
		uint32_t			numberOfServices = m_serviceConfiguration->GetServices().size();
		RandomVariable		serviceSelector = UniformVariable(0, 100);


		for(uint32_t i = 0; i<numberOfServices; i++)
		{
			servicesDependenceProbabilities[i] = serviceSelector.GetValue();
		}
	}

	void AddClientExecutionStepsWithDecreasingDependenceProbabilityToAllServices (
			RandomVariable requestSize,
			double servicesDependenceProbabilities [])
	{
		const map<uint32_t, Ptr<Client> > & 				clients = m_serviceConfiguration->GetClients();
		Ptr<Client>											client;
		map<uint32_t, Ptr<Client> >::const_iterator			x;
		map<uint32_t, Ptr<ServiceMethod> >::const_iterator 	y;
		Ptr<ServiceMethod>									antecedent;
		double												stepProbability;


		for (x = clients.begin(); x != clients.end(); x++)
		{
			for (y = m_serviceMethods.begin(); y != m_serviceMethods.end(); y++)
			{
				client = x->second;
				antecedent = y->second;

				stepProbability = servicesDependenceProbabilities[antecedent->GetService()->GetServiceId() - 1];

				m_serviceConfiguration->AddClientExecutionStep(
						client->GetServiceId(),
						antecedent->GetService()->GetContractId(),
						antecedent->GetContractMethodId(),
						requestSize,
						stepProbability);
			}
		}
	}

	void AddClientExecutionStepsWithRandomFixedDependenceProbabilityToNServices (
			RandomVariable requestSize,
			RandomVariable stepProbability,
			uint32_t servicesToBeUsedByClients [],
			uint32_t numberOfServicesToBeUsedByClients)
	{
		const map<uint32_t, Ptr<Client> > & 				clients = m_serviceConfiguration->GetClients();
		Ptr<Client>											client;
		uint32_t											serviceId;
		map<uint32_t, Ptr<Client> >::const_iterator			x;
		map<uint32_t, Ptr<ServiceMethod> >::const_iterator 	y;
		Ptr<ServiceMethod>									antecedent;


		for (x = clients.begin(); x != clients.end(); x++)
		{
			for (y = m_serviceMethods.begin(); y != m_serviceMethods.end(); y++)
			{
				client = x->second;
				antecedent = y->second;
				serviceId = antecedent->GetService()->GetServiceId();

				// check if the method's service is in set of services used by clients
				// this aproach is nasty but better then dealing with const qualifiers when accessing list of services
				for (uint32_t i = 0; i<numberOfServicesToBeUsedByClients; i++)
				{
					//NS_LOG_UNCOND(serviceId << ' ' << i << ' ' << servicesToBeUsedByClients [i]);

					if (servicesToBeUsedByClients [i] == serviceId)
					{
						m_serviceConfiguration->AddClientExecutionStep(
								client->GetServiceId(),
								antecedent->GetService()->GetContractId(),
								antecedent->GetContractMethodId(),
								requestSize,
								stepProbability.GetValue());
						break;
					}
				}
			}
		}
	}

}; // ServiceConfigurationRandomGenerator




struct NodeAssignment
{
	static const uint32_t NODE_ASSIGNMENT_NOT_FOUND = UINT_MAX;
	uint32_t nodeId;
	uint32_t serviceId;
};




class SimulationLoader : public Object
{
private:
	const NodeContainer 				m_nodes;
	const Ptr<SimulationOutput>			m_simulationOutput;
	const Ptr<ServiceConfiguration>		m_serviceConfiguration;
	const uint16_t						m_servicePortBaseId;
	const NodeAssignment * 	 			m_fixedNodeAssignments;
	const uint32_t						m_fixedNodeAssignmentsSize;
	const bool							m_writeOut;
	ApplicationContainer 				m_clientContainer;
	ApplicationContainer 				m_serviceContainer;

public:

	SimulationLoader (
			NodeContainer nodes,
			Ptr<SimulationOutput> simulationOutput,
			Ptr<ServiceConfiguration> serviceConfiguration,
			uint16_t servicePortBaseId,
			const NodeAssignment * fixedNodeAssignments,
			const uint32_t fixedNodeAssignmentsSize,
			bool writeOut)
		:m_nodes (nodes),
		 m_simulationOutput(simulationOutput),
		 m_serviceConfiguration (serviceConfiguration),
		 m_servicePortBaseId (servicePortBaseId),
		 m_fixedNodeAssignments(fixedNodeAssignments),
		 m_fixedNodeAssignmentsSize(fixedNodeAssignmentsSize),
		 m_writeOut (writeOut)
	{
		NS_ASSERT(nodes.GetN() > 0);
		NS_ASSERT(simulationOutput != NULL);
		NS_ASSERT(serviceConfiguration != NULL);
		NS_ASSERT(servicePortBaseId != 0);
	}

	virtual ~SimulationLoader() {}

	void LoadServiceConfiguration ()
	{
		 if (!m_serviceConfiguration->CheckServiceConfiguration())
		 {
			 NS_LOG_UNCOND("ServiceConfiguration check didnt pass, ServiceConfiguration cant be loaded!");
		 }
		 else
		 {
			 InstantiateClients();
			 InstantiateServices();
		 }
	}

private:

	uint32_t RandomSelectNodeForDeployment ()
	{
		static RandomVariable distributionVariable = UniformVariable (0, m_nodes.GetN() - 1);

		return distributionVariable.GetInteger();
	}

	void InstantiateClients ()
	{
		const map<uint32_t, Ptr<Client> > &				clients = m_serviceConfiguration->GetClients();
		map<uint32_t, Ptr<Client> >::const_iterator		it;
		bool 											deployClientsRandomly = m_serviceConfiguration->GetDeployClientsRandomly();
		uint32_t										sequentialNodeId = 0;


		if (m_writeOut)
		{
			NS_LOG_UNCOND("Instantiating clients ...");
		}

		for (it = clients.begin(); it != clients.end(); it++)
		{
			InstantiateClient(it->second, deployClientsRandomly, sequentialNodeId);
			sequentialNodeId++;
		}
	}

	void InstantiateClient (Ptr<Client> client, bool deployClientsRandomly, uint32_t sequentialNodeId)
	{
		NS_ASSERT(client != NULL);

		Ptr<ClientInstance>		clientInstance = CreateObject<ClientInstance>(client, m_simulationOutput);
		uint32_t				nodeId;
		Ptr<Node> 				node;


		nodeId = GetFixedNodeAssignment(client->GetServiceId());

		if (nodeId == NodeAssignment::NODE_ASSIGNMENT_NOT_FOUND)
		{
			if (deployClientsRandomly)
			{
				nodeId = RandomSelectNodeForDeployment();
			}
			else
			{
				nodeId = sequentialNodeId;
			}
		}

		node = m_nodes.Get(nodeId);

		if (m_writeOut)
		{
			Ptr<ClientExecutionPlan> plan = DynamicCast<ClientExecutionPlan>(client->GetExecutionPlan());


			NS_LOG_UNCOND(
					"	Client: " << client->GetServiceId()
					<< ", node: " << nodeId
					<< ", start: " << client->GetStartTime()
					<< ", stop: " << client->GetStopTime()
					<< ", steps: " << plan->GetExecutionStepsCount()
					<< ", request period: " << plan->GetRequestRate());

			WriteOutSimulation_ExecutionPlan(client->GetExecutionPlan());
		}

		node->AddApplication(clientInstance);
		m_clientContainer.Add(clientInstance);

		clientInstance->SetStartTime(client->GetStartTime());
		clientInstance->SetStopTime(client->GetStopTime());
	}

	uint32_t GetFixedNodeAssignment(uint32_t serviceId)
	{
		for(uint32_t i = 0; i < m_fixedNodeAssignmentsSize; i++)
		{
			if (m_fixedNodeAssignments[i].serviceId == serviceId)
			{
					return m_fixedNodeAssignments[i].nodeId;
			}
		}

		return NodeAssignment::NODE_ASSIGNMENT_NOT_FOUND;
	}

	void WriteOutSimulation_ExecutionPlan (const Ptr<ExecutionPlan> plan) const
	{
		NS_ASSERT(plan != NULL);

		vector<Ptr<ExecutionStep> >::const_iterator		esit;
		Ptr<ExecutionStep>									step;


		for (esit = plan->GetExecutionSteps().begin(); esit != plan->GetExecutionSteps().end(); esit++)
		{
			step = *esit;

			NS_LOG_UNCOND(
					"			Execution Step - contract: " << step->GetContractId()
					<< ", method: " << step->GetContractMethodId()
					<< ", step probability: " << step->GetStepProbability());
//					<< ", request size: " << step->GetRequestSize());
		}
	}

	void WriteOutSimulation_ServiceMethods(Ptr<Service> service)
	{
		NS_ASSERT(service != NULL);

		map<uint32_t, Ptr<ServiceMethod> >::const_iterator 	smit;
		Ptr<ServiceMethod>									method;


		for (smit = service->GetMethods().begin(); smit != service->GetMethods().end(); smit++)
		{
			method = smit->second;

			NS_LOG_UNCOND(
					"		Service method: " << method->GetContractMethodId()
					<< ", error model: " << method->GetFaultModel()
					<< ", steps: " << method->GetExecutionPlan()->GetExecutionStepsCount()
					<< ", plan pre delay: " << method->GetExecutionPlan()->GetPlanPreExeDelay()
					<< ", plan post delay: " << method->GetExecutionPlan()->GetPlanPostExeDelay()
					<< ", step post delay: " << method->GetExecutionPlan()->GetStepPostExeDelay());

			WriteOutSimulation_ExecutionPlan(method->GetExecutionPlan());
		}
	}

	void InstantiateServices ()
	{
		const map<uint32_t, Ptr<Service> > &				services = m_serviceConfiguration->GetServices();
		map<uint32_t, Ptr<Service> >::const_iterator		it;
		set<pair<uint32_t, uint32_t> >						contractToNodeAssignments;


		if (m_writeOut)
		{
			NS_LOG_UNCOND("Instantiating services ...");
		}

		for (it = services.begin(); it != services.end(); it++)
		{
			InstantiateService(it->second, contractToNodeAssignments);
		}
	}

	void InstantiateService (
			Ptr<Service> service,
			set<pair<uint32_t, uint32_t> > & contractToNodeAssignments)
	{
		NS_ASSERT(service != NULL);

		Ptr<ServiceInstance>		serviceInstance;
		uint32_t					nodeId;
		Ptr<Node> 					node;
		uint16_t					portId;


		nodeId = FindNodeForServiceDeployment(service, contractToNodeAssignments);
		node = m_nodes.Get(nodeId);
		portId = m_servicePortBaseId + node->GetNApplications();
		serviceInstance = CreateObject<ServiceInstance>(service, portId, m_simulationOutput);

		if (m_writeOut)
		{
			NS_LOG_UNCOND(
					"	Service: " << service->GetServiceId()
					<< ", contract: " << service->GetContractId()
					<< ", node: " << nodeId
					<< ", start: " << service->GetStartTime()
					<< ", stop: " << service->GetStopTime()
					<< ", error model: " << service->GetFaultModel()
					<< ", methods: " << service->GetMethods().size());

			WriteOutSimulation_ServiceMethods(service);
		}

		node->AddApplication(serviceInstance);
		m_serviceContainer.Add(serviceInstance);

		serviceInstance->SetStartTime(service->GetStartTime());
		serviceInstance->SetStopTime(service->GetStopTime());
	}

	uint32_t FindNodeForServiceDeployment(
			Ptr<Service> service,
			set<pair<uint32_t, uint32_t> > & contractToNodeAssignments)
	{
		NS_ASSERT(service != NULL);

		Ptr<ServiceInstance>		serviceInstance;
		uint32_t					nodeId;
		pair<uint32_t, uint32_t>	contractToNode;


		nodeId = GetFixedNodeAssignment(service->GetServiceId());

		if (nodeId != NodeAssignment::NODE_ASSIGNMENT_NOT_FOUND)
		{
			return nodeId;
		}

		while(true)
		{
			nodeId = RandomSelectNodeForDeployment();
			contractToNode = pair<uint32_t, uint32_t>(service->GetContractId(), nodeId);

			if (contractToNodeAssignments.find(contractToNode) == contractToNodeAssignments.end())
			{
				contractToNodeAssignments.insert(contractToNode);
				return nodeId;
			}
		}

		return NULL;
	}

}; // SimulationLoader

class NetworkConfigurationGenerator
{
protected:
	NodeContainer 				m_nodes;
	NodeContainer 				m_mobileNodes;
	NodeContainer 				m_staticNodes;

	NetworkConfigurationGenerator () {}
	virtual ~NetworkConfigurationGenerator () {}

public:

	NodeContainer GetNodes ()
	{
		NS_ASSERT(m_nodes.GetN() != 0);

		return m_nodes;
	}

	NodeContainer GetMobileNodes ()
	{
		NS_ASSERT(m_mobileNodes.GetN() != 0);

		return m_nodes;
	}

	NodeContainer GetStaticNodes ()
	{
		NS_ASSERT(m_staticNodes.GetN() != 0);

		return m_nodes;
	}

}; // NetworkConfigurationGenerator

/*
class CsmaNetworkConfigurationGenerator : public NetworkConfigurationGenerator
{
public:

	CsmaNetworkConfigurationGenerator () {}
	virtual ~CsmaNetworkConfigurationGenerator () {}


	void GenerateNetwork (uint32_t numberOfNodes)
	{
		NS_ASSERT(numberOfNodes != 0);

		NS_LOG_UNCOND("Network generation started ...");
		NS_LOG_UNCOND("	Network type: CSMA");
		NS_LOG_UNCOND("	Number of nodes: " << numberOfNodes);

		m_nodes.Create(numberOfNodes);

		CsmaHelper csma;
		csma.SetChannelAttribute ("DataRate", StringValue ("100Mbps"));
		csma.SetChannelAttribute ("Delay", TimeValue (NanoSeconds (6560)));

		NetDeviceContainer csmaDevices;
		csmaDevices = csma.Install (m_nodes);

		InternetStackHelper stack;
		stack.Install (m_nodes);

		Ipv4AddressHelper address;

		address.SetBase ("10.1.2.0", "255.255.255.0");
		Ipv4InterfaceContainer csmaInterfaces;
		csmaInterfaces = address.Assign (csmaDevices);

		Ipv4GlobalRoutingHelper::PopulateRoutingTables ();
	}

}; // CsmaNetworkConfigurationGenerator

*/
class AdHocMobileNetworkConfigurationGenerator : public NetworkConfigurationGenerator
{
public:

	AdHocMobileNetworkConfigurationGenerator () {}
	virtual ~AdHocMobileNetworkConfigurationGenerator () {}


	void GenerateNetwork (uint32_t numberOfNodes)
	{
		NS_ASSERT(numberOfNodes != 0);

		NS_LOG_UNCOND("Network layer generation started ...");
		NS_LOG_UNCOND("	Network type: AdHoc Mobile");
		NS_LOG_UNCOND("	Number of nodes: " << numberOfNodes);

		m_nodes.Create(numberOfNodes);


		// wifi and adhoc configuration

		std::string phyMode ("wifib-1mbs");
		double rss = -80;  // -dBm


		// disable fragmentation for frames below 2200 bytes
		Config::SetDefault ("ns3::WifiRemoteStationManager::FragmentationThreshold", StringValue ("2200"));
		// turn off RTS/CTS for frames below 2200 bytes
		Config::SetDefault ("ns3::WifiRemoteStationManager::RtsCtsThreshold", StringValue ("2200"));
		// Fix non-unicast data rate to be the same as that of unicast
		Config::SetDefault ("ns3::WifiRemoteStationManager::NonUnicastMode",
						  StringValue (phyMode));


		// The below set of helpers will help us to put together the wifi NICs we want
		WifiHelper wifi;
		  //wifi.EnableLogComponents ();  // Turn on all Wifi logging
		wifi.SetStandard (WIFI_PHY_STANDARD_80211b);

		YansWifiPhyHelper wifiPhy =  YansWifiPhyHelper::Default ();
		// This is one parameter that matters when using FixedRssLossModel
		// set it to zero; otherwise, gain will be added
		wifiPhy.Set ("RxGain", DoubleValue (0) );
		// ns-3 support RadioTap and Prism tracing extensions for 802.11b
		//wifiPhy.SetPcapFormat (YansWifiPhyHelper::PCAP_FORMAT_80211_RADIOTAP);

		YansWifiChannelHelper wifiChannel ;
		wifiChannel.SetPropagationDelay ("ns3::ConstantSpeedPropagationDelayModel");
		// The below FixedRssLossModel will cause the rss to be fixed regardless
		// of the distance between the two stations, and the transmit power
		wifiChannel.AddPropagationLoss ("ns3::FixedRssLossModel","Rss",DoubleValue(rss));
		wifiPhy.SetChannel (wifiChannel.Create ());

		// Add a non-QoS upper mac, and disable rate control
		NqosWifiMacHelper wifiMac = NqosWifiMacHelper::Default ();
		wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager",
									"DataMode",StringValue(phyMode),
									   "ControlMode",StringValue(phyMode));
		// Set it to adhoc mode
		wifiMac.SetType ("ns3::AdhocWifiMac");
		NetDeviceContainer devices = wifi.Install (wifiPhy, wifiMac, m_nodes);


		// mobility configuration

		// Note that with FixedRssLossModel, the positions below are not
		// used for received signal strength.
		MobilityHelper mobility;
		int gridSize = 10; //10x10 grid  for a total of 100 nodes
		int nodeDistance = 30;

		mobility.SetPositionAllocator ("ns3::GridPositionAllocator",
									"MinX", DoubleValue (0.0),
									"MinY", DoubleValue (0.0),
									"DeltaX", DoubleValue (nodeDistance),
									"DeltaY", DoubleValue (nodeDistance),
									"GridWidth", UintegerValue (gridSize),
									"LayoutType", StringValue ("RowFirst"));

		mobility.SetMobilityModel ("ns3::RandomDirection2dMobilityModel",
								  "Bounds", RectangleValue (Rectangle (0, 500, 0, 500)),
								  "Speed", RandomVariableValue (ConstantVariable (10)),
								  "Pause", RandomVariableValue (ConstantVariable (0.2)));

		mobility.Install (m_nodes);

		// routing configuration
		OlsrHelper olsr;
		Ipv4StaticRoutingHelper staticRouting;
		Ipv4ListRoutingHelper list;
		list.Add (staticRouting, 0);
		list.Add (olsr, 10);

		InternetStackHelper internet;
		internet.SetRoutingHelper(list);
		internet.Install (m_nodes);

		Ipv4AddressHelper ipv4;
		ipv4.SetBase ("10.1.1.0", "255.255.255.0");
		Ipv4InterfaceContainer i = ipv4.Assign (devices);
	}

	void GenerateNetworkMANET (
			uint32_t numberOfNodes,
			uint32_t gridXLength,
			uint32_t gridYLength,
			double mobilitySpeed)
		{
			NS_ASSERT(numberOfNodes != 0);
			NS_ASSERT(gridXLength != 0);
			NS_ASSERT(gridYLength != 0);

			NS_LOG_UNCOND("Network layer generation started ...");
			NS_LOG_UNCOND("	Network type: AdHoc Mobile 80211b");
			NS_LOG_UNCOND("	Number of nodes: " << numberOfNodes);
			NS_LOG_UNCOND("	Grid size X axe: " << gridXLength);
			NS_LOG_UNCOND("	Grid size y axe: " << gridYLength);

			m_nodes.Create(numberOfNodes);


			// wifi and adhoc configuration

			StringValue phyMode = StringValue ("DsssRate11Mbps");
			//StringValue phyMode = StringValue ("OfdmRate24Mbps");
			//WifiModeValue phyMode = WifiModeValue(WifiPhy::GetOfdmRate54Mbps());


			// disable fragmentation for frames below 2200 bytes
			Config::SetDefault ("ns3::WifiRemoteStationManager::FragmentationThreshold", StringValue ("2200"));
			// turn off RTS/CTS for frames below 2200 bytes
			Config::SetDefault ("ns3::WifiRemoteStationManager::RtsCtsThreshold", StringValue ("2200"));
			// Fix non-unicast data rate to be the same as that of unicast
			Config::SetDefault ("ns3::WifiRemoteStationManager::NonUnicastMode", phyMode);

			// The below set of helpers will help us to put together the wifi NICs we want
			WifiHelper wifi;
			  //wifi.EnableLogComponents ();  // Turn on all Wifi logging
			wifi.SetStandard (WIFI_PHY_STANDARD_80211b);
			//wifi.SetStandard (WIFI_PHY_STANDARD_80211g);

			YansWifiPhyHelper wifiPhy =  YansWifiPhyHelper::Default ();
			// This is one parameter that matters when using FixedRssLossModel
			// set it to zero; otherwise, gain will be added
			wifiPhy.Set ("RxGain", DoubleValue (0) );
			// ns-3 support RadioTap and Prism tracing extensions for 802.11b
			//wifiPhy.SetPcapFormat (YansWifiPhyHelper::PCAP_FORMAT_80211_RADIOTAP);

			YansWifiChannelHelper wifiChannel; // = YansWifiChannelHelper::Default();
			wifiChannel.SetPropagationDelay ("ns3::ConstantSpeedPropagationDelayModel");
			NS_LOG_UNCOND("	Propagation delay model: ConstantSpeedPropagationDelayModel");
			//wifiChannel.AddPropagationLoss ("ns3::FriisPropagationLossModel");
			//wifiChannel.AddPropagationLoss ("ns3::FixedRssLossModel","Rss",DoubleValue(-80));
			NS_LOG_UNCOND("	Propagation loss model: LogDistancePropagationLossModel - exp 3");
			wifiChannel.AddPropagationLoss ("ns3::LogDistancePropagationLossModel", "Exponent", DoubleValue(3));
			//NS_LOG_UNCOND("	Propagation loss model: NakagamiPropagationLossModel");
			//wifiChannel.AddPropagationLoss ("ns3::NakagamiPropagationLossModel");

			wifiPhy.SetChannel (wifiChannel.Create ());

			// Add a non-QoS upper mac, and disable rate control
			NqosWifiMacHelper wifiMac = NqosWifiMacHelper::Default ();
			wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager",
										"DataMode", phyMode,
										   "ControlMode", phyMode);
			// Set it to adhoc mode
			wifiMac.SetType ("ns3::AdhocWifiMac");
			NetDeviceContainer devices = wifi.Install (wifiPhy, wifiMac, m_nodes);


			// mobility configuration
			// Note that with FixedRssLossModel, the positions below are not used for received signal strength.

			MobilityHelper mobility;

			NS_LOG_UNCOND("	Position allocator: RandomRectanglePositionAllocator");

			mobility.SetPositionAllocator ("ns3::RandomRectanglePositionAllocator",
													"X", RandomVariableValue(UniformVariable (0, gridXLength)),
													"Y", RandomVariableValue(UniformVariable (0, gridYLength)));

			if (mobilitySpeed == 0)
			{
				mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");

				NS_LOG_UNCOND("	Mobility model: ConstantPositionMobilityModel");
			}
			else
			{
				mobility.SetMobilityModel ("ns3::RandomDirection2dMobilityModel",
										  "Bounds", RectangleValue (Rectangle (0, gridXLength, 0, gridYLength)),
										  "Speed", RandomVariableValue (ConstantVariable (mobilitySpeed)),
										  "Pause", RandomVariableValue (ConstantVariable (0.2)));

				NS_LOG_UNCOND("	Mobility model: RandomDirection2dMobilityModel");
				NS_LOG_UNCOND("		Speed: " << mobilitySpeed);
				NS_LOG_UNCOND("		Pause: " << ConstantVariable (0.2));
				NS_LOG_UNCOND("		Bounds: " << Rectangle (0, gridXLength, 0, gridYLength));
			}

			mobility.Install (m_nodes);

			//AttachMobilityCourseChangeTracer(m_nodes);

			// routing configuration
			OlsrHelper olsr;
			Ipv4StaticRoutingHelper staticRouting;
			Ipv4ListRoutingHelper list;
//			list.Add (staticRouting, 0);
			list.Add (olsr, 0);

			InternetStackHelper internet;
			internet.SetRoutingHelper(list);
			internet.Install (m_nodes);

			Ipv4AddressHelper ipv4;
			ipv4.SetBase ("10.1.1.0", "255.255.255.0");
			Ipv4InterfaceContainer i = ipv4.Assign (devices);
	}

	void GenerateNetworkHybrid (
			uint32_t numberOfMobileNodes,
			uint32_t numberOfStaticNodes,
			uint32_t gridXLength,
			uint32_t gridYLength,
			uint32_t gridXLengthModifierForStaticNodes,
			uint32_t gridYLengthModifierForStaticNodes,
			double mobilitySpeed,
			RandomVariable mobilityPause)
		{
			NS_ASSERT(numberOfMobileNodes != 0);
			NS_ASSERT(numberOfStaticNodes != 0);
			NS_ASSERT(gridXLength != 0);
			NS_ASSERT(gridYLength != 0);

			NS_LOG_UNCOND("Network layer generation started ...");
			NS_LOG_UNCOND("	Network type: Hybrid - based on AdHoc Mobile 80211b");
			NS_LOG_UNCOND("	Number of mobile nodes: " << numberOfMobileNodes);
			NS_LOG_UNCOND("	Number of static nodes: " << numberOfStaticNodes);
			NS_LOG_UNCOND("	Grid size X axe: " << gridXLength);
			NS_LOG_UNCOND("	Grid size y axe: " << gridYLength);
			NS_LOG_UNCOND("	Grid size X lenght modifier for static nodes: " << gridXLengthModifierForStaticNodes);
			NS_LOG_UNCOND("	Grid size Y lenght modifier for static nodes: " << gridYLengthModifierForStaticNodes);


			m_mobileNodes.Create(numberOfMobileNodes);
			m_staticNodes.Create(numberOfStaticNodes);
			//m_nodes.Create(numberOfNodes);


			// wifi and adhoc configuration

			StringValue phyMode = StringValue ("DsssRate11Mbps");
			//StringValue phyMode = StringValue ("OfdmRate24Mbps");
			//WifiModeValue phyMode = WifiModeValue(WifiPhy::GetOfdmRate54Mbps());


			// disable fragmentation for frames below 2200 bytes
			Config::SetDefault ("ns3::WifiRemoteStationManager::FragmentationThreshold", StringValue ("2200"));
			// turn off RTS/CTS for frames below 2200 bytes
			Config::SetDefault ("ns3::WifiRemoteStationManager::RtsCtsThreshold", StringValue ("2200"));
			// Fix non-unicast data rate to be the same as that of unicast
			Config::SetDefault ("ns3::WifiRemoteStationManager::NonUnicastMode", phyMode);

			// The below set of helpers will help us to put together the wifi NICs we want
			WifiHelper wifi;
			  //wifi.EnableLogComponents ();  // Turn on all Wifi logging
			wifi.SetStandard (WIFI_PHY_STANDARD_80211b);
			//wifi.SetStandard (WIFI_PHY_STANDARD_80211g);

			YansWifiPhyHelper wifiPhy =  YansWifiPhyHelper::Default ();
			// This is one parameter that matters when using FixedRssLossModel
			// set it to zero; otherwise, gain will be added
			wifiPhy.Set ("RxGain", DoubleValue (0) );
			// ns-3 support RadioTap and Prism tracing extensions for 802.11b
			//wifiPhy.SetPcapFormat (YansWifiPhyHelper::PCAP_FORMAT_80211_RADIOTAP);

			YansWifiChannelHelper wifiChannel; // = YansWifiChannelHelper::Default();
			wifiChannel.SetPropagationDelay ("ns3::ConstantSpeedPropagationDelayModel");
			NS_LOG_UNCOND("	Propagation delay model: ConstantSpeedPropagationDelayModel");
			//wifiChannel.AddPropagationLoss ("ns3::FriisPropagationLossModel");
			//wifiChannel.AddPropagationLoss ("ns3::FixedRssLossModel","Rss",DoubleValue(-80));
			NS_LOG_UNCOND("	Propagation loss model: LogDistancePropagationLossModel - exp 2");
			wifiChannel.AddPropagationLoss ("ns3::LogDistancePropagationLossModel", "Exponent", DoubleValue(2));
			//NS_LOG_UNCOND("	Propagation loss model: NakagamiPropagationLossModel");
			//wifiChannel.AddPropagationLoss ("ns3::NakagamiPropagationLossModel");

			wifiPhy.SetChannel (wifiChannel.Create ());

			// Add a non-QoS upper mac, and disable rate control
			NqosWifiMacHelper wifiMac = NqosWifiMacHelper::Default ();
			wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager",
										"DataMode", phyMode,
										   "ControlMode", phyMode);


			// Set it to adhoc mode
			//wifiMac.SetType ("ns3::AdhocWifiMac");
			//NetDeviceContainer devices = wifi.Install (wifiPhy, wifiMac, m_nodes);
			// !!moved below

			// mobility configuration
			// Note that with FixedRssLossModel, the positions below are not used for received signal strength.


			// Mobile part of the network
			MobilityHelper mobility;

			NS_LOG_UNCOND("	Mobile nodes ...");
			NS_LOG_UNCOND("	Position allocator: RandomRectanglePositionAllocator");

			mobility.SetPositionAllocator ("ns3::RandomRectanglePositionAllocator",
													"X", RandomVariableValue(UniformVariable (0, gridXLength)),
													"Y", RandomVariableValue(UniformVariable (0, gridYLength)));

			if (mobilitySpeed == 0)
			{
				mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");

				NS_LOG_UNCOND("	Mobility model: ConstantPositionMobilityModel");
			}
			else
			{
				mobility.SetMobilityModel ("ns3::RandomDirection2dMobilityModel",
										  "Bounds", RectangleValue (Rectangle (0, gridXLength, 0, gridYLength)),
										  "Speed", RandomVariableValue (ConstantVariable (mobilitySpeed)),
										  "Pause", RandomVariableValue (mobilityPause));

				NS_LOG_UNCOND("	Mobility model: RandomDirection2dMobilityModel");
				NS_LOG_UNCOND("		Speed: " << mobilitySpeed);
				NS_LOG_UNCOND("		Pause: " << mobilityPause);
				NS_LOG_UNCOND("		Bounds: " << Rectangle (0, gridXLength, 0, gridYLength));
			}

			mobility.Install (m_mobileNodes);




			// Static part of the network

			NS_LOG_UNCOND("	Static nodes ...");
			NS_LOG_UNCOND("	Position allocator: RandomRectanglePositionAllocator");

			mobility.SetPositionAllocator ("ns3::RandomRectanglePositionAllocator",
													"X", RandomVariableValue(UniformVariable (
															gridXLengthModifierForStaticNodes, gridXLength - gridXLengthModifierForStaticNodes)),
													"Y", RandomVariableValue(UniformVariable (
															gridYLengthModifierForStaticNodes, gridYLength - gridYLengthModifierForStaticNodes)));

			mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");

			NS_LOG_UNCOND("	Mobility model: ConstantPositionMobilityModel");

			mobility.Install (m_staticNodes);




			// routing
			m_nodes.Add(m_mobileNodes);
			m_nodes.Add(m_staticNodes);

			// Set it to adhoc mode
			wifiMac.SetType ("ns3::AdhocWifiMac");
			NetDeviceContainer devices = wifi.Install (wifiPhy, wifiMac, m_nodes);

			//AttachMobilityCourseChangeTracer(m_nodes);

			// routing configuration
			OlsrHelper olsr;
			Ipv4StaticRoutingHelper staticRouting;
			Ipv4ListRoutingHelper list;
//			list.Add (staticRouting, 0);
			list.Add (olsr, 0);

			InternetStackHelper internet;
			internet.SetRoutingHelper(list);
			internet.Install (m_nodes);

			Ipv4AddressHelper ipv4;
			ipv4.SetBase ("10.1.1.0", "255.255.255.0");
			Ipv4InterfaceContainer i = ipv4.Assign (devices);
	}

	void GenerateNetworkMANETGrid (
			uint32_t numberOfNodes,
			uint32_t gridXLength,
			uint32_t gridYLength,
			double mobilitySpeed)
		{
			NS_ASSERT(numberOfNodes != 0);
			NS_ASSERT(gridXLength != 0);
			NS_ASSERT(gridYLength != 0);

			NS_LOG_UNCOND("Network layer generation started ...");
			NS_LOG_UNCOND("	Network type: AdHoc Mobile 80211b - grid");
			NS_LOG_UNCOND("	Number of nodes: " << numberOfNodes);
			NS_LOG_UNCOND("	Grid size X axe: " << gridXLength);
			NS_LOG_UNCOND("	Grid size y axe: " << gridYLength);

			m_nodes.Create(numberOfNodes);




			  std::string phyMode ("DsssRate11Mbps");
			  double distance = 100;  // m


			  // disable fragmentation for frames below 2200 bytes
			  Config::SetDefault ("ns3::WifiRemoteStationManager::FragmentationThreshold", StringValue ("2200"));
			  // turn off RTS/CTS for frames below 2200 bytes
			  Config::SetDefault ("ns3::WifiRemoteStationManager::RtsCtsThreshold", StringValue ("2200"));
			  // Fix non-unicast data rate to be the same as that of unicast
			  Config::SetDefault ("ns3::WifiRemoteStationManager::NonUnicastMode",
			                      StringValue (phyMode));


			  // The below set of helpers will help us to put together the wifi NICs we want
			  WifiHelper wifi;

			  YansWifiPhyHelper wifiPhy =  YansWifiPhyHelper::Default ();
			  // set it to zero; otherwise, gain will be added
			  //wifiPhy.Set ("RxGain", DoubleValue (-10) );
			  wifiPhy.Set ("RxGain", DoubleValue (0) );

			  YansWifiChannelHelper wifiChannel;
			  wifiChannel.SetPropagationDelay ("ns3::ConstantSpeedPropagationDelayModel");
			  wifiChannel.AddPropagationLoss ("ns3::FriisPropagationLossModel");
				//wifiChannel.AddPropagationLoss ("ns3::RangePropagationLossModel", "MaxRange", DoubleValue(500));

			  wifiPhy.SetChannel (wifiChannel.Create ());

			  // Add a non-QoS upper mac, and disable rate control
			  NqosWifiMacHelper wifiMac = NqosWifiMacHelper::Default ();
			  wifi.SetStandard (WIFI_PHY_STANDARD_80211b);
			  wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager",
			                                "DataMode",StringValue (phyMode),
			                                "ControlMode",StringValue (phyMode));
			  // Set it to adhoc mode
			  wifiMac.SetType ("ns3::AdhocWifiMac");
			  NetDeviceContainer devices = wifi.Install (wifiPhy, wifiMac, m_nodes);

			  MobilityHelper mobility;
			  mobility.SetPositionAllocator ("ns3::GridPositionAllocator",
			                                 "MinX", DoubleValue (0.0),
			                                 "MinY", DoubleValue (0.0),
			                                 "DeltaX", DoubleValue (distance),
			                                 "DeltaY", DoubleValue (distance),
			                                 "GridWidth", UintegerValue (10),
			                                 "LayoutType", StringValue ("RowFirst"));
			  mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
			  mobility.Install (m_nodes);

			  // Enable OLSR
			  OlsrHelper olsr;
			  Ipv4StaticRoutingHelper staticRouting;

			  Ipv4ListRoutingHelper list;
			  list.Add (staticRouting, 0);
			  list.Add (olsr, 10);

			  InternetStackHelper internet;
			  internet.SetRoutingHelper (list); // has effect on the next Install ()
			  internet.Install (m_nodes);

			  Ipv4AddressHelper ipv4;
			  ipv4.SetBase ("10.1.1.0", "255.255.255.0");
			  Ipv4InterfaceContainer i = ipv4.Assign (devices);

	}


	void GenerateNetworkMANET80211bWithRangePropagationLossModel (
			uint32_t numberOfNodes,
			uint32_t gridXLength,
			uint32_t gridYLength,
			double mobilitySpeed)
		{
		NS_ASSERT(numberOfNodes != 0);
		NS_ASSERT(gridXLength != 0);
		NS_ASSERT(gridYLength != 0);

		NS_LOG_UNCOND("Network layer generation started ...");
		NS_LOG_UNCOND("	Network type: AdHoc Mobile 80211b");
		NS_LOG_UNCOND("	Number of nodes: " << numberOfNodes);
		NS_LOG_UNCOND("	Grid size X axe: " << gridXLength);
		NS_LOG_UNCOND("	Grid size y axe: " << gridYLength);

/*
		uint32_t numberOfNodes = 50;
		uint32_t gridXLength = 1000;
		uint32_t gridYLength = 1000;
		double mobilitySpeed = 10;
*/

		m_nodes.Create(numberOfNodes);



		StringValue phyMode = StringValue ("DsssRate2Mbps");

		WifiHelper wifi;
		wifi.SetStandard (WIFI_PHY_STANDARD_80211b);

		YansWifiPhyHelper wifiPhy =  YansWifiPhyHelper::Default ();

		YansWifiChannelHelper wifiChannel ;
		wifiChannel.SetPropagationDelay ("ns3::ConstantSpeedPropagationDelayModel");
		wifiChannel.AddPropagationLoss ("ns3::RangePropagationLossModel", "MaxRange", DoubleValue(500));

		wifiPhy.SetChannel (wifiChannel.Create ());

		NqosWifiMacHelper wifiMac = NqosWifiMacHelper::Default ();
		wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager",
			"DataMode", phyMode,
			"ControlMode", phyMode);

		wifiMac.SetType ("ns3::AdhocWifiMac");
		NetDeviceContainer devices = wifi.Install (wifiPhy, wifiMac, m_nodes);

		MobilityHelper mobility;

		mobility.SetPositionAllocator ("ns3::RandomRectanglePositionAllocator",
			"X", RandomVariableValue(UniformVariable (0, gridXLength)),
			"Y", RandomVariableValue(UniformVariable (0, gridYLength)));

		mobility.SetMobilityModel ("ns3::RandomDirection2dMobilityModel",
			"Bounds", RectangleValue (Rectangle (0, gridXLength, 0, gridYLength)),
			"Speed", RandomVariableValue (ConstantVariable (mobilitySpeed)),
			"Pause", RandomVariableValue (ConstantVariable (0.2)));

		mobility.Install (m_nodes);

		OlsrHelper olsr;
		Ipv4StaticRoutingHelper staticRouting;
		Ipv4ListRoutingHelper list;
		list.Add (staticRouting, 0);
		list.Add (olsr, 10);

		InternetStackHelper internet;
		internet.SetRoutingHelper(list);
		internet.Install (m_nodes);

		Ipv4AddressHelper ipv4;
		ipv4.SetBase ("10.1.1.0", "255.255.255.0");
		Ipv4InterfaceContainer i = ipv4.Assign (devices);
	}

void GenerateNetworkMANET80211b (
	uint32_t numberOfNodes,
	uint32_t gridXLength,
	uint32_t gridYLength,
	double mobilitySpeed)
{
	m_nodes.Create(numberOfNodes);

	StringValue phyMode = StringValue ("DsssRate11Mbps");
	//StringValue phyMode = StringValue ("ErpOfdmRate54Mbps");


	Config::SetDefault ("ns3::WifiRemoteStationManager::FragmentationThreshold", StringValue ("2200"));
	Config::SetDefault ("ns3::WifiRemoteStationManager::RtsCtsThreshold", StringValue ("2200"));
	Config::SetDefault ("ns3::WifiRemoteStationManager::NonUnicastMode", phyMode);

	WifiHelper wifi;

	wifi.SetStandard (WIFI_PHY_STANDARD_80211b);
	//wifi.SetStandard (WIFI_PHY_STANDARD_80211g);

	YansWifiPhyHelper wifiPhy =  YansWifiPhyHelper::Default ();
	wifiPhy.Set ("RxGain", DoubleValue (0) );

	YansWifiChannelHelper wifiChannel ;
	wifiChannel.SetPropagationDelay ("ns3::ConstantSpeedPropagationDelayModel");
	wifiChannel.AddPropagationLoss ("ns3::LogDistancePropagationLossModel", "Exponent", DoubleValue(3));

	wifiPhy.SetChannel (wifiChannel.Create ());

	NqosWifiMacHelper wifiMac = NqosWifiMacHelper::Default ();
	wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager",
								"DataMode", phyMode,
								   "ControlMode", phyMode);

	wifiMac.SetType ("ns3::AdhocWifiMac");
	NetDeviceContainer devices = wifi.Install (wifiPhy, wifiMac, m_nodes);


	MobilityHelper mobility;

	mobility.SetPositionAllocator ("ns3::RandomRectanglePositionAllocator",
									"X", RandomVariableValue(UniformVariable (0, gridXLength)),
									"Y", RandomVariableValue(UniformVariable (0, gridYLength)));

	mobility.SetMobilityModel ("ns3::RandomDirection2dMobilityModel",
							  "Bounds", RectangleValue (Rectangle (0, gridXLength, 0, gridYLength)),
							  "Speed", RandomVariableValue (ConstantVariable (mobilitySpeed)),
							  "Pause", RandomVariableValue (ConstantVariable (0.2)));

	mobility.Install (m_nodes);

	OlsrHelper olsr;
	Ipv4StaticRoutingHelper staticRouting;
	Ipv4ListRoutingHelper list;
	list.Add (staticRouting, 0);
	list.Add (olsr, 10);

	InternetStackHelper internet;
	internet.SetRoutingHelper(list);
	internet.Install (m_nodes);

	Ipv4AddressHelper ipv4;
	ipv4.SetBase ("10.1.1.0", "255.255.255.0");
	Ipv4InterfaceContainer i = ipv4.Assign (devices);
}

	void GenerateNetworkMANET80211g (
			uint32_t numberOfNodes,
			uint32_t gridXLength,
			uint32_t gridYLength,
			double mobilitySpeed)
		{
			NS_ASSERT(numberOfNodes != 0);
			NS_ASSERT(gridXLength != 0);
			NS_ASSERT(gridYLength != 0);

			NS_LOG_UNCOND("Network layer generation started ...");
			NS_LOG_UNCOND("	Network type: AdHoc Mobile 80211g");
			NS_LOG_UNCOND("	Number of nodes: " << numberOfNodes);
			NS_LOG_UNCOND("	Grid size X axe: " << gridXLength);
			NS_LOG_UNCOND("	Grid size y axe: " << gridYLength);

			m_nodes.Create(numberOfNodes);






			WifiHelper wifi = WifiHelper::Default ();


			wifi.SetStandard (WIFI_PHY_STANDARD_80211g);

			/* doesnt work
			wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager",
					"DataMode", StringValue ("ErpOfdmRate54Mbps"));
			 */

			wifi.SetRemoteStationManager ("ns3::ArfWifiManager");


			// disable fragmentation for frames below 2200 bytes
			Config::SetDefault ("ns3::WifiRemoteStationManager::FragmentationThreshold", StringValue ("2200"));
			// turn off RTS/CTS for frames below 2200 bytes
			Config::SetDefault ("ns3::WifiRemoteStationManager::RtsCtsThreshold", StringValue ("2200"));
			// Fix non-unicast data rate to be the same as that of unicast

			//			Config::SetDefault ("ns3::WifiRemoteStationManager::NonUnicastMode", StringValue ("ErpOfdmRate54Mbps"));
			// doesnt work


			  NqosWifiMacHelper wifiMac = NqosWifiMacHelper::Default ();
			  YansWifiPhyHelper wifiPhy = YansWifiPhyHelper::Default ();
			  wifiPhy.Set ("RxGain", DoubleValue (0) );

			 // YansWifiChannelHelper wifiChannel = YansWifiChannelHelper::Default();


				YansWifiChannelHelper wifiChannel ;
				wifiChannel.SetPropagationDelay ("ns3::ConstantSpeedPropagationDelayModel");
				wifiChannel.AddPropagationLoss ("ns3::LogDistancePropagationLossModel", "Exponent", DoubleValue(3));


			  wifiMac.SetType ("ns3::AdhocWifiMac");


			  YansWifiPhyHelper phy = wifiPhy;
			  phy.SetChannel (wifiChannel.Create ());

			  NqosWifiMacHelper mac = wifiMac;
			  NetDeviceContainer devices = wifi.Install (phy, mac, m_nodes);

			  /*
			  MobilityHelper mobility;
			  Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator> ();
			  positionAlloc->Add (Vector (0.0, 0.0, 0.0));
			  positionAlloc->Add (Vector (100.0, 0.0, 0.0));
			  mobility.SetPositionAllocator (positionAlloc);
			  mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");

			  mobility.Install (m_nodes);
*/

				// mobility configuration
				// Note that with FixedRssLossModel, the positions below are not used for received signal strength.

				MobilityHelper mobility;

				NS_LOG_UNCOND("	Position allocator: RandomRectanglePositionAllocator");

				mobility.SetPositionAllocator ("ns3::RandomRectanglePositionAllocator",
														"X", RandomVariableValue(UniformVariable (0, gridXLength)),
														"Y", RandomVariableValue(UniformVariable (0, gridYLength)));

				if (mobilitySpeed == 0)
				{
					mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");

					NS_LOG_UNCOND("	Mobility model: ConstantPositionMobilityModel");
				}
				else
				{
					mobility.SetMobilityModel ("ns3::RandomDirection2dMobilityModel",
											  "Bounds", RectangleValue (Rectangle (0, gridXLength, 0, gridYLength)),
											  "Speed", RandomVariableValue (ConstantVariable (mobilitySpeed)),
											  "Pause", RandomVariableValue (ConstantVariable (0.2)));

					NS_LOG_UNCOND("	Mobility model: RandomDirection2dMobilityModel");
					NS_LOG_UNCOND("		Speed: " << mobilitySpeed);
					NS_LOG_UNCOND("		Pause: " << ConstantVariable (0.2));
					NS_LOG_UNCOND("		Bounds: " << Rectangle (0, gridXLength, 0, gridYLength));
				}

				mobility.Install (m_nodes);

				//AttachMobilityCourseChangeTracer(m_nodes);

				// routing configuration
				OlsrHelper olsr;
				Ipv4StaticRoutingHelper staticRouting;
				Ipv4ListRoutingHelper list;
				list.Add (staticRouting, 0);
				list.Add (olsr, 10);

				InternetStackHelper internet;
				internet.SetRoutingHelper(list);
				internet.Install (m_nodes);

				Ipv4AddressHelper ipv4;
				ipv4.SetBase ("10.1.1.0", "255.255.255.0");
				Ipv4InterfaceContainer i = ipv4.Assign (devices);


			  //PacketSocketAddress socket;
			  //socket.SetSingleDevice (devices.Get (0)->GetIfIndex ());
			  //socket.SetPhysicalAddress (devices.Get (1)->GetAddress ());
			  //socket.SetProtocol (1);

/*
			  OnOffHelper onoff ("ns3::PacketSocketFactory", Address (socket));
			  onoff.SetAttribute ("OnTime", RandomVariableValue (ConstantVariable
			(250)));
			  onoff.SetAttribute ("OffTime", RandomVariableValue (ConstantVariable
			(10)));
			  onoff.SetAttribute ("DataRate", DataRateValue (DataRate
			(600000000)));
			  onoff.SetAttribute ("PacketSize", UintegerValue (2000));


			  ApplicationContainer apps = onoff.Install (c.Get (0));
			  apps.Start (Seconds (0.5));
			  apps.Stop (Seconds (30));


			  phy.SetPcapDataLinkType(YansWifiPhyHelper::DLT_IEEE802_11_RADIO);


			  phy.EnablePcap ("new", devices.Get(0));
			  phy.EnablePcap ("new", devices.Get(1));


			  Simulator::Stop(Seconds(30));
			  Simulator::Run ();
			  Simulator::Destroy ();
			  return 0;
*/





/*



			Config::SetDefault ("ns3::WifiRemoteStationManager::FragmentationThreshold", StringValue ("2200"));
			Config::SetDefault ("ns3::WifiRemoteStationManager::RtsCtsThreshold", StringValue ("2200"));

			WifiHelper wifi = WifiHelper::Default ();
			wifi.SetStandard (WIFI_PHY_STANDARD_80211g);
			NqosWifiMacHelper wifiMac = NqosWifiMacHelper::Default ();
			YansWifiPhyHelper wifiPhy = YansWifiPhyHelper::Default ();
			YansWifiChannelHelper wifiChannel = YansWifiChannelHelper::Default ();

			wifiMac.SetType ("ns3::AdhocWifiMac");

			wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager",
					"DataMode", StringValue ("ErpOfdmRate54Mbps"));


			YansWifiPhyHelper phy = wifiPhy;
			phy.SetChannel (wifiChannel.Create ());

			NqosWifiMacHelper mac = wifiMac;
			NetDeviceContainer devices = wifi.Install (phy, mac, m_nodes);

			MobilityHelper mobility;

			ObjectFactory pos;
			pos.SetTypeId ("ns3::RandomRectanglePositionAllocator");
			pos.Set ("X", RandomVariableValue (UniformVariable (0.0, 1000)));
			pos.Set ("Y", RandomVariableValue (UniformVariable (0.0, 1000)));
			Ptr<PositionAllocator> taPositionAlloc = pos.Create ()->GetObject<PositionAllocator> ();
			mobility.SetMobilityModel ("ns3::RandomWaypointMobilityModel",
									 "Speed", RandomVariableValue (UniformVariable (0.56, 2.78)), // tussen 2 en 10 km/u
									 "Pause", RandomVariableValue (UniformVariable (0, 1200)), // 20 min blijven staan
									 "PositionAllocator", PointerValue (taPositionAlloc));
			mobility.SetPositionAllocator (taPositionAlloc);

			mobility.Install (m_nodes);

			// routing configuration
			OlsrHelper olsr;
			Ipv4StaticRoutingHelper staticRouting;
			Ipv4ListRoutingHelper list;
			list.Add (staticRouting, 0);
			list.Add (olsr, 10);

			InternetStackHelper internet;
			internet.SetRoutingHelper(list);
			internet.Install (m_nodes);

			Ipv4AddressHelper ipv4;
			ipv4.SetBase ("10.1.1.0", "255.255.255.0");
			Ipv4InterfaceContainer i = ipv4.Assign (devices);
*/

			/*
			PacketSocketAddress socket;
			socket.SetAllDevices();
			socket.SetPhysicalAddress (Mac48Address::GetBroadcast());
			socket.SetProtocol (1);


			InternetStackHelper stack;
			stack.Install (m_nodes);

*/
/*

			// wifi and adhoc configuration

			StringValue phyMode = StringValue ("ErpOfdmRate54Mbps");
			//StringValue phyMode = StringValue ("OfdmRate24Mbps");
			//WifiModeValue phyMode = WifiModeValue(WifiPhy::GetOfdmRate54Mbps());


			// disable fragmentation for frames below 2200 bytes
			Config::SetDefault ("ns3::WifiRemoteStationManager::FragmentationThreshold", StringValue ("2200"));
			// turn off RTS/CTS for frames below 2200 bytes
			Config::SetDefault ("ns3::WifiRemoteStationManager::RtsCtsThreshold", StringValue ("2200"));
			// Fix non-unicast data rate to be the same as that of unicast
			Config::SetDefault ("ns3::WifiRemoteStationManager::NonUnicastMode", phyMode);

			// The below set of helpers will help us to put together the wifi NICs we want
			WifiHelper wifi;
			  //wifi.EnableLogComponents ();  // Turn on all Wifi logging
			//wifi.SetStandard (WIFI_PHY_STANDARD_80211b);
			wifi.SetStandard (WIFI_PHY_STANDARD_80211g);

			YansWifiPhyHelper wifiPhy =  YansWifiPhyHelper::Default ();
			// This is one parameter that matters when using FixedRssLossModel
			// set it to zero; otherwise, gain will be added
			wifiPhy.Set ("RxGain", DoubleValue (0) );
			// ns-3 support RadioTap and Prism tracing extensions for 802.11b
			//wifiPhy.SetPcapFormat (YansWifiPhyHelper::PCAP_FORMAT_80211_RADIOTAP);

			YansWifiChannelHelper wifiChannel ;
			wifiChannel.SetPropagationDelay ("ns3::ConstantSpeedPropagationDelayModel");
			NS_LOG_UNCOND("	Propagation delay model: ConstantSpeedPropagationDelayModel");
			//wifiChannel.AddPropagationLoss ("ns3::FixedRssLossModel","Rss",DoubleValue(-80));
			NS_LOG_UNCOND("	Propagation loss model: LogDistancePropagationLossModel - exp 3");
			wifiChannel.AddPropagationLoss ("ns3::LogDistancePropagationLossModel", "Exponent", DoubleValue(3));
			//NS_LOG_UNCOND("	Propagation loss model: NakagamiPropagationLossModel");
			//wifiChannel.AddPropagationLoss ("ns3::NakagamiPropagationLossModel");

			wifiPhy.SetChannel (wifiChannel.Create ());

			// Add a non-QoS upper mac, and disable rate control
			NqosWifiMacHelper wifiMac = NqosWifiMacHelper::Default ();
			wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager",
										"DataMode", phyMode,
										   "ControlMode", phyMode);
			// Set it to adhoc mode
			wifiMac.SetType ("ns3::AdhocWifiMac");
			NetDeviceContainer devices = wifi.Install (wifiPhy, wifiMac, m_nodes);


			// mobility configuration
			// Note that with FixedRssLossModel, the positions below are not used for received signal strength.

			MobilityHelper mobility;

			NS_LOG_UNCOND("	Position allocator: RandomRectanglePositionAllocator");

			mobility.SetPositionAllocator ("ns3::RandomRectanglePositionAllocator",
													"X", RandomVariableValue(UniformVariable (0, gridXLength)),
													"Y", RandomVariableValue(UniformVariable (0, gridYLength)));

			if (mobilitySpeed == 0)
			{
				mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");

				NS_LOG_UNCOND("	Mobility model: ConstantPositionMobilityModel");
			}
			else
			{
				mobility.SetMobilityModel ("ns3::RandomDirection2dMobilityModel",
										  "Bounds", RectangleValue (Rectangle (0, gridXLength, 0, gridYLength)),
										  "Speed", RandomVariableValue (ConstantVariable (mobilitySpeed)),
										  "Pause", RandomVariableValue (ConstantVariable (0.2)));

				NS_LOG_UNCOND("	Mobility model: RandomDirection2dMobilityModel");
				NS_LOG_UNCOND("		Speed: " << mobilitySpeed);
				NS_LOG_UNCOND("		Pause: " << ConstantVariable (0.2));
				NS_LOG_UNCOND("		Bounds: " << Rectangle (0, gridXLength, 0, gridYLength));
			}

			mobility.Install (m_nodes);

			//AttachMobilityCourseChangeTracer(m_nodes);

			// routing configuration
			OlsrHelper olsr;
			Ipv4StaticRoutingHelper staticRouting;
			Ipv4ListRoutingHelper list;
			list.Add (staticRouting, 0);
			list.Add (olsr, 10);

			InternetStackHelper internet;
			internet.SetRoutingHelper(list);
			internet.Install (m_nodes);

			Ipv4AddressHelper ipv4;
			ipv4.SetBase ("10.1.1.0", "255.255.255.0");
			Ipv4InterfaceContainer i = ipv4.Assign (devices);
			*/
	}


	static void MobileNodeCourseChange (std::string context, Ptr<const MobilityModel> model)
	{
	  Vector position = model->GetPosition ();
	  NS_LOG_UNCOND (Simulator::Now() << ": " <<  context << " x = " << (int)position.x << ", y = " << (int)position.y);
	}

	static void AttachMobilityCourseChangeTracer (NodeContainer nodes)
	{
		Ptr<Node> node;


		for(uint32_t i = 0; i < nodes.GetN(); i++)
		{
			node = nodes.Get(i);

			std::ostringstream context;
			context << "/NodeList/" << node->GetId () << "/$ns3::MobilityModel/CourseChange";

			Config::Connect (
					context.str(),
					MakeCallback (&AdHocMobileNetworkConfigurationGenerator::MobileNodeCourseChange));
		}
	}

	void GenerateNetworkLossTest (uint32_t numberOfNodes, uint32_t mobilitySpeed)
		{
			NS_ASSERT(numberOfNodes != 0);

			NS_LOG_UNCOND("Network layer generation started ...");
			NS_LOG_UNCOND("	Network type: AdHoc Mobile (Loss test)");
			NS_LOG_UNCOND("	Number of nodes: " << numberOfNodes);
			NS_LOG_UNCOND("	Mobility speed: " << mobilitySpeed);

			m_nodes.Create(numberOfNodes);


			// wifi and adhoc configuration

			std::string phyMode ("wifib-1mbs");
			//double rss = -80;  // -dBm


			// disable fragmentation for frames below 2200 bytes
			Config::SetDefault ("ns3::WifiRemoteStationManager::FragmentationThreshold", StringValue ("2200"));
			// turn off RTS/CTS for frames below 2200 bytes
			Config::SetDefault ("ns3::WifiRemoteStationManager::RtsCtsThreshold", StringValue ("2200"));
			// Fix non-unicast data rate to be the same as that of unicast
			Config::SetDefault ("ns3::WifiRemoteStationManager::NonUnicastMode",
							  StringValue (phyMode));


			// The below set of helpers will help us to put together the wifi NICs we want
			WifiHelper wifi;
			  //wifi.EnableLogComponents ();  // Turn on all Wifi logging
			//wifi.SetStandard (WIFI_PHY_STANDARD_80211b);

			YansWifiPhyHelper wifiPhy =  YansWifiPhyHelper::Default ();
			// This is one parameter that matters when using FixedRssLossModel
			// set it to zero; otherwise, gain will be added
			wifiPhy.Set ("RxGain", DoubleValue (0) );
			// ns-3 support RadioTap and Prism tracing extensions for 802.11b
			//wifiPhy.SetPcapFormat (YansWifiPhyHelper::PCAP_FORMAT_80211_RADIOTAP);

			YansWifiChannelHelper wifiChannel ;
			wifiChannel.SetPropagationDelay ("ns3::ConstantSpeedPropagationDelayModel");
			// The below FixedRssLossModel will cause the rss to be fixed regardless
			// of the distance between the two stations, and the transmit power
			//wifiChannel.AddPropagationLoss ("ns3::FixedRssLossModel","Rss",DoubleValue(rss));
			wifiChannel.AddPropagationLoss ("ns3::NakagamiPropagationLossModel");
			wifiPhy.SetChannel (wifiChannel.Create ());

			// Add a non-QoS upper mac, and disable rate control
			NqosWifiMacHelper wifiMac = NqosWifiMacHelper::Default ();
			wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager",
										"DataMode",StringValue(phyMode),
										   "ControlMode",StringValue(phyMode));
			// Set it to adhoc mode
			wifiMac.SetType ("ns3::AdhocWifiMac");
			NetDeviceContainer devices = wifi.Install (wifiPhy, wifiMac, m_nodes);


			// mobility configuration

			// Note that with FixedRssLossModel, the positions below are not
			// used for received signal strength.
			MobilityHelper mobility;
			int gridSize = 10; //10x10 grid  for a total of 100 nodes
			int nodeDistance = 30;

			mobility.SetPositionAllocator ("ns3::GridPositionAllocator",
										"MinX", DoubleValue (0.0),
										"MinY", DoubleValue (0.0),
										"DeltaX", DoubleValue (nodeDistance),
										"DeltaY", DoubleValue (nodeDistance),
										"GridWidth", UintegerValue (gridSize),
										"LayoutType", StringValue ("RowFirst"));

			mobility.SetMobilityModel ("ns3::RandomDirection2dMobilityModel",
									  "Bounds", RectangleValue (Rectangle (0, 1000, 0, 1000)),
									  "Speed", RandomVariableValue (ConstantVariable (mobilitySpeed)),
									  "Pause", RandomVariableValue (ConstantVariable (0.2)));

			mobility.Install (m_nodes);

			AttachMobilityCourseChangeTracer(m_nodes);

			// routing configuration
			OlsrHelper olsr;
			Ipv4StaticRoutingHelper staticRouting;
			Ipv4ListRoutingHelper list;
			list.Add (staticRouting, 0);
			list.Add (olsr, 10);

			InternetStackHelper internet;
			internet.SetRoutingHelper(list);
			internet.Install (m_nodes);

			Ipv4AddressHelper ipv4;
			ipv4.SetBase ("10.1.1.0", "255.255.255.0");
			Ipv4InterfaceContainer i = ipv4.Assign (devices);
		}

}; // AdHocMobileNetworkConfigurationGenerator



class ServiceConfigurationGeneratorFactory
{
public:
/*
	static ServiceConfigurationRandomGenerator Create1to1 ()
	{
		Ptr<FaultModel> on = CreateObject<SingleRateFaultModel>(true, true, 0, UniformVariable (0., 1.));
		Ptr<FaultModel> off = CreateObject<SingleRateFaultModel>(true, true, 0, UniformVariable (0., 1.));

		ServiceConfigurationRandomGenerator scrg;

		scrg.GenerateServices(
			1, //uint32_t numberOfServices,
			1, //uint32_t serviceBaseId,
			1001, //uint32_t contractBaseId,
			ConstantVariable(1000), //RandomVariable startTime,
			ConstantVariable(1000000), //RandomVariable stopTime,
			MilliSeconds(5000), //Time responseTimeout,
			MilliSeconds(50), //Time ACKTimeout,
			5, //uint32_t retransmissionLimit,
			MilliSeconds(5000), //Time msgIdLifetime,
			off, //Ptr<FaultModel> serviceFaultModel,
			ConstantVariable(1), //RandomVariable numberOfServiceMethods,
			ConstantVariable(500), //RandomVariable methodResponseSize,
			off, //Ptr<FaultModel> methodFaultModel,
			ConstantVariable(10), //RandomVariable methodPreExeDelay,
			ConstantVariable(20), //RandomVariable methodPostExeDelay,
			1, //double executionStepDependencyProbability,
			ConstantVariable(10), //RandomVariable executionStepPostExeDelay,
			ConstantVariable(300)); //RandomVariable executionStepRequestSize)

		scrg.GenerateClients (
			1, //uint32_t numberOfClients,
			100001, //uint32_t clientBaseId,
			ConstantVariable(2000), //RandomVariable startTime,
			ConstantVariable(100000), //RandomVariable stopTime,
			MilliSeconds(5000), //Time responseTimeout,
			MilliSeconds(50), //Time ACKTimeout,
			5, //uint32_t retransmissionLimit,
			MilliSeconds(5000), //Time msgIdLifetime,
			ConstantVariable(1000), //RandomVariable planRequestRate,
			1, //double executionStepDependencyProbability,
			ConstantVariable(300), //RandomVariable executionStepRequestSize
			ConstantVariable(30000)); //RandomVariable afterFailureWaitingPeriod

		return scrg;
	}
*/
	/*
	static ServiceConfigurationRandomGenerator Create2to2 ()
	{
		Ptr<FaultModel> off = CreateObject<SingleRateFaultModel>(true, true, 0, UniformVariable (0., 1.));

		ServiceConfigurationRandomGenerator scrg;

		scrg.GenerateServices(
			2, //uint32_t numberOfServices,
			1, //uint32_t serviceBaseId,
			1001, //uint32_t contractBaseId,
			ConstantVariable(1000), //RandomVariable startTime,
			ConstantVariable(1000000), //RandomVariable stopTime,
			MilliSeconds(5000), //Time responseTimeout,
			MilliSeconds(50), //Time ACKTimeout,
			5, //uint32_t retransmissionLimit,
			MilliSeconds(5000), //Time msgIdLifetime,
			off, //Ptr<FaultModel> serviceFaultModel,
			ConstantVariable(1), //RandomVariable numberOfServiceMethods,
			ConstantVariable(500), //RandomVariable methodResponseSize,
			off, //Ptr<FaultModel> methodFaultModel,
			ConstantVariable(10), //RandomVariable methodPreExeDelay,
			ConstantVariable(20), //RandomVariable methodPostExeDelay,
			1, //double executionStepDependencyProbability,
			ConstantVariable(10), //RandomVariable executionStepPostExeDelay,
			ConstantVariable(300)); //RandomVariable executionStepRequestSize)

		scrg.GenerateClients (
			2, //uint32_t numberOfClients,
			100001, //uint32_t clientBaseId,
			UniformVariable(1500, 2000), //RandomVariable startTime,
			ConstantVariable(100000), //RandomVariable stopTime,
			MilliSeconds(5000), //Time responseTimeout,
			MilliSeconds(50), //Time ACKTimeout,
			5, //uint32_t retransmissionLimit,
			MilliSeconds(5000), //Time msgIdLifetime,
			ConstantVariable(1000), //RandomVariable planRequestRate,
			1, //double executionStepDependencyProbability,
			ConstantVariable(300), //RandomVariable executionStepRequestSize
			ConstantVariable(30000)); //RandomVariable afterFailureWaitingPeriod

		return scrg;
	}
*/

	static ServiceConfigurationRandomGenerator CreateUniformRandomScenario (
			uint32_t numberOfServices,
			RandomVariable numberOfServiceMethods,
			double probabilityOfServiceToServiceConnection,
			uint32_t numberOfClients,
			double probabilityOfClientToServiceConnection,
			RandomVariable clientRequestRate)
	{
		NS_LOG_UNCOND("Service layer generation started ...");
		NS_LOG_UNCOND("	Scenario: UniformRandom");
		NS_LOG_UNCOND("	Number of services: " << numberOfServices);
		NS_LOG_UNCOND("	Number of service methods: " << numberOfServiceMethods);
		NS_LOG_UNCOND("	Probability of service to service connection: " << probabilityOfServiceToServiceConnection);
		NS_LOG_UNCOND("	Number of clients: " << numberOfClients);
		NS_LOG_UNCOND("	Probability of client to service connection: " << probabilityOfClientToServiceConnection);
		NS_LOG_UNCOND("	Client request rate: " << clientRequestRate);


		Ptr<FaultModel> offFaultModel = CreateObject<SingleRateFaultModel>(false, true, 0, UniformVariable (0., 1.));
		Ptr<FaultModel> serviceFaultModel = CreateObject<OnOffRateFaultModel>(
				false, //bool isEnabled,
				true, //bool isGeneratingException
				false, //bool state,
				0.001, //double offRate,
				UniformVariable (0., 1.), //RandomVariable offRanvar,
				0.1, //double onRate,
				UniformVariable (0., 1.)); //RandomVariable onRanvar

		ServiceConfigurationRandomGenerator scrg;


		scrg.GenerateServices(
			numberOfServices, //uint32_t numberOfServices,
			1, //uint32_t serviceBaseId,
			1, //uint32_t contractBaseId,
			ConstantVariable(2), //RandomVariable numberOfReplicas,
			ConstantVariable(100), //RandomVariable startTime,
			ConstantVariable(1000000000), //RandomVariable stopTime,
			MilliSeconds(60000), //Time responseTimeout
			MilliSeconds(1000), //Time ACKTimeout
			5, //uint32_t retransmissionLimit,
			MilliSeconds(75000), //Time msgIdLifetime
			serviceFaultModel, //Ptr<FaultModel> serviceFaultModel,
			numberOfServiceMethods, //RandomVariable numberOfServiceMethods,
			UniformVariable(500, 1500), //RandomVariable methodResponseSize,
			offFaultModel, //Ptr<FaultModel> methodFaultModel,
			ConstantVariable(20), //20 RandomVariable methodPreExeDelay,
			ConstantVariable(20), //20 RandomVariable methodPostExeDelay,
			ConstantVariable (10), //10 RandomVariable methodPostPlanErrorDelay,
			probabilityOfServiceToServiceConnection, //double executionStepDependencyProbability,
			ConstantVariable(10), //10 RandomVariable executionStepPostExeDelay,
			UniformVariable(500, 1500), //RandomVariable executionStepRequestSize
			UniformVariable (0, 100), //RandomVariable stepProbability
			ConstantVariable (10)); //10 RandomVariable servicePostErrorDelay

		scrg.GenerateClientsWithUniformDependenceProbability (
			false, // bool deployClientsRandomly,
			numberOfClients, //uint32_t numberOfClients,
			100001, //uint32_t clientBaseId,
			UniformVariable(200, 500), //RandomVariable startTime,
			ConstantVariable(1000000000), //RandomVariable stopTime,
			MilliSeconds(60000), //Time responseTimeout, standard 20 000
			MilliSeconds(1000), //Time ACKTimeout,
			5, //uint32_t retransmissionLimit,
			MilliSeconds(75000), //Time msgIdLifetime, standard 45 000
			clientRequestRate, //RandomVariable planRequestRate,
			probabilityOfClientToServiceConnection, //double executionStepDependencyProbability,
			UniformVariable(500, 1500), //RandomVariable executionStepRequestSize
			clientRequestRate); //RandomVariable afterFailureWaitingPeriod standard 30000

		return scrg;
	}

	static ServiceConfigurationRandomGenerator CreateWithRandomFixedDependenceProbabilityToAllMethodsScenario (
			uint32_t numberOfServices,
			RandomVariable numberOfServiceMethods,
			double probabilityOfServiceToServiceConnection,
			uint32_t numberOfClients,
			RandomVariable clientRequestRate)
	{
		NS_LOG_UNCOND("Service layer generation started ...");
		NS_LOG_UNCOND("	Scenario: WithRandomFixedDependenceProbabilityToAllMethods");
		NS_LOG_UNCOND("	Number of services: " << numberOfServices);
		NS_LOG_UNCOND("	Number of service methods: " << numberOfServiceMethods);
		NS_LOG_UNCOND("	Probability of service to service connection: " << probabilityOfServiceToServiceConnection);
		NS_LOG_UNCOND("	Number of clients: " << numberOfClients);
		NS_LOG_UNCOND("	Client request rate: " << clientRequestRate);


		Ptr<FaultModel> offFaultModel = CreateObject<SingleRateFaultModel>(false, true, 0, UniformVariable (0., 1.));
		Ptr<FaultModel> serviceFaultModel = CreateObject<OnOffRateFaultModel>(
				false, //bool isEnabled,
				true, //bool isGeneratingException
				false, //bool state,
				0.001, //double offRate,
				UniformVariable (0., 1.), //RandomVariable offRanvar,
				0.1, //double onRate,
				UniformVariable (0., 1.)); //RandomVariable onRanvar

		ServiceConfigurationRandomGenerator scrg;


		scrg.GenerateServices(
			numberOfServices, //uint32_t numberOfServices,
			1, //uint32_t serviceBaseId,
			1, //uint32_t contractBaseId,
			ConstantVariable(2), //RandomVariable numberOfReplicas,
			ConstantVariable(100), //RandomVariable startTime,
			ConstantVariable(1000000000), //RandomVariable stopTime,
			MilliSeconds(60000), //Time responseTimeout, standard 20 000
			MilliSeconds(1000), //Time ACKTimeout,
			5, //uint32_t retransmissionLimit,
			MilliSeconds(75000), //Time msgIdLifetime, standard 45 000
			serviceFaultModel, //Ptr<FaultModel> serviceFaultModel,
			numberOfServiceMethods, //RandomVariable numberOfServiceMethods,
			UniformVariable(500, 1500), //RandomVariable methodResponseSize,
			offFaultModel, //Ptr<FaultModel> methodFaultModel,
			ConstantVariable(20), //RandomVariable methodPreExeDelay,
			ConstantVariable(20), //RandomVariable methodPostExeDelay,
			ConstantVariable (10), //RandomVariable methodPostPlanErrorDelay,
			probabilityOfServiceToServiceConnection, //double executionStepDependencyProbability,
			ConstantVariable(10), //RandomVariable executionStepPostExeDelay,
			UniformVariable(500, 1500), //RandomVariable executionStepRequestSize
			UniformVariable (0, 100), //RandomVariable stepProbability
			ConstantVariable (10)); //RandomVariable servicePostErrorDelay

		scrg.GenerateClientsWithRandomFixedDependenceProbabilityToAllMethods (
			false, // bool deployClientsRandomly,
			numberOfClients, //uint32_t numberOfClients,
			100001, //uint32_t clientBaseId,
			UniformVariable(200, 500), //RandomVariable startTime,
			ConstantVariable(1000000000), //RandomVariable stopTime,
			MilliSeconds(60000), //Time responseTimeout, standard 20 000
			MilliSeconds(1000), //Time ACKTimeout,
			5, //uint32_t retransmissionLimit,
			MilliSeconds(75000), //Time msgIdLifetime, standard 45 000
			clientRequestRate, //RandomVariable planRequestRate,
			UniformVariable(500, 1500), //RandomVariable executionStepRequestSize
			clientRequestRate, //RandomVariable afterFailureWaitingPeriod standard 30000
			UniformVariable (0, 100)); //RandomVariable stepProbability

		return scrg;
	}

	static ServiceConfigurationRandomGenerator CreateWithDecreasingDependenceProbabilityToAllServicesScenario (
			uint32_t numberOfServices,
			RandomVariable numberOfServiceMethods,
			double probabilityOfServiceToServiceConnection,
			uint32_t numberOfClients,
			RandomVariable clientRequestRate)
	{
		NS_LOG_UNCOND("Service layer generation started ...");
		NS_LOG_UNCOND("	Scenario: WithDecreasingDependenceProbabilityToAllServices");
		NS_LOG_UNCOND("	Number of services: " << numberOfServices);
		NS_LOG_UNCOND("	Number of service methods: " << numberOfServiceMethods);
		NS_LOG_UNCOND("	Probability of service to service connection: " << probabilityOfServiceToServiceConnection);
		NS_LOG_UNCOND("	Number of clients: " << numberOfClients);
		NS_LOG_UNCOND("	Client request rate: " << clientRequestRate);


		Ptr<FaultModel> offFaultModel = CreateObject<SingleRateFaultModel>(false, true, 0, UniformVariable (0., 1.));
		Ptr<FaultModel> serviceFaultModel = CreateObject<OnOffRateFaultModel>(
				false, //bool isEnabled,
				true, //bool isGeneratingException
				false, //bool state,
				0.001, //double offRate,
				UniformVariable (0., 1.), //RandomVariable offRanvar,
				0.1, //double onRate,
				UniformVariable (0., 1.)); //RandomVariable onRanvar

		ServiceConfigurationRandomGenerator scrg;


		scrg.GenerateServices(
			numberOfServices, //uint32_t numberOfServices,
			1, //uint32_t serviceBaseId,
			1, //uint32_t contractBaseId,
			ConstantVariable(2), //RandomVariable numberOfReplicas,
			ConstantVariable(100), //RandomVariable startTime,
			ConstantVariable(1000000000), //RandomVariable stopTime,
			MilliSeconds(60000), //Time responseTimeout, standard 20 000
			MilliSeconds(1000), //Time ACKTimeout,
			5, //uint32_t retransmissionLimit,
			MilliSeconds(75000), //Time msgIdLifetime, standard 45 000
			serviceFaultModel, //Ptr<FaultModel> serviceFaultModel,
			numberOfServiceMethods, //RandomVariable numberOfServiceMethods,
			UniformVariable(500, 1500), //RandomVariable methodResponseSize,
			offFaultModel, //Ptr<FaultModel> methodFaultModel,
			ConstantVariable(20), //RandomVariable methodPreExeDelay,
			ConstantVariable(20), //RandomVariable methodPostExeDelay,
			ConstantVariable (10), //RandomVariable methodPostPlanErrorDelay,
			probabilityOfServiceToServiceConnection, //double executionStepDependencyProbability,
			ConstantVariable(10), //RandomVariable executionStepPostExeDelay,
			UniformVariable(500, 1500), //RandomVariable executionStepRequestSize)
			UniformVariable (0, 100), //RandomVariable stepProbability
			ConstantVariable (10)); //RandomVariable servicePostErrorDelay

		scrg.GenerateClientsWithDecreasingDependenceProbabilityToAllServices (
			false, // bool deployClientsRandomly,
			numberOfClients, //uint32_t numberOfClients,
			100001, //uint32_t clientBaseId,
			UniformVariable(200, 500), //RandomVariable startTime,
			ConstantVariable(1000000000), //RandomVariable stopTime,
			MilliSeconds(60000), //Time responseTimeout, standard 20 000
			MilliSeconds(1000), //Time ACKTimeout,
			5, //uint32_t retransmissionLimit,
			MilliSeconds(75000), //Time msgIdLifetime, standard 45 000
			clientRequestRate, //RandomVariable planRequestRate,
			UniformVariable(500, 1500), //RandomVariable executionStepRequestSize
			clientRequestRate); //RandomVariable afterFailureWaitingPeriod standard 30000

		return scrg;
	}

	static ServiceConfigurationRandomGenerator CreateWithRandomFixedDependenceProbabilityToNServicesScenario (
			uint32_t numberOfServices,
			RandomVariable numberOfServiceMethods,
			double probabilityOfServiceToServiceConnection,
			uint32_t numberOfClients,
			RandomVariable clientRequestRate,
			uint32_t numberOfServicesToBeUsedByClients)
	{
		NS_LOG_UNCOND("Service layer generation started ...");
		NS_LOG_UNCOND("	Scenario: WithRandomFixedDependenceProbabilityToNServices");
		NS_LOG_UNCOND("	Number of services: " << numberOfServices);
		NS_LOG_UNCOND("	Number of service methods: " << numberOfServiceMethods);
		NS_LOG_UNCOND("	Probability of service to service connection: " << probabilityOfServiceToServiceConnection);
		NS_LOG_UNCOND("	Number of clients: " << numberOfClients);
		NS_LOG_UNCOND("	Client request rate: " << clientRequestRate);
		NS_LOG_UNCOND("	Number of services to be used by clients: " << numberOfServicesToBeUsedByClients);


		Ptr<FaultModel> offFaultModel = CreateObject<SingleRateFaultModel>(false, true, 0, UniformVariable (0., 1.));
		Ptr<FaultModel> serviceFaultModel = CreateObject<OnOffRateFaultModel>(
				false, //bool isEnabled,
				true, //bool isGeneratingException
				false, //bool state,
				0.001, //double offRate,
				UniformVariable (0., 1.), //RandomVariable offRanvar,
				0.1, //double onRate,
				UniformVariable (0., 1.)); //RandomVariable onRanvar

		ServiceConfigurationRandomGenerator scrg;


		scrg.GenerateServices(
			numberOfServices, //uint32_t numberOfServices,
			1, //uint32_t serviceBaseId,
			1, //uint32_t contractBaseId,
			ConstantVariable(2), //RandomVariable numberOfReplicas,
			ConstantVariable(100), //RandomVariable startTime,
			ConstantVariable(1000000000), //RandomVariable stopTime,
			MilliSeconds(60000), //Time responseTimeout, standard 20 000
			MilliSeconds(1000), //Time ACKTimeout,
			5, //uint32_t retransmissionLimit,
			MilliSeconds(75000), //Time msgIdLifetime, standard 45 000
			serviceFaultModel, //Ptr<FaultModel> serviceFaultModel,
			numberOfServiceMethods, //RandomVariable numberOfServiceMethods,
			UniformVariable(500, 1500), //RandomVariable methodResponseSize,
			offFaultModel, //Ptr<FaultModel> methodFaultModel,
			ConstantVariable(20), //RandomVariable methodPreExeDelay,
			ConstantVariable(20), //RandomVariable methodPostExeDelay,
			ConstantVariable (10), //RandomVariable methodPostPlanErrorDelay,
			probabilityOfServiceToServiceConnection, //double executionStepDependencyProbability,
			ConstantVariable(10), //RandomVariable executionStepPostExeDelay,
			UniformVariable(500, 1500), //RandomVariable executionStepRequestSize)
			UniformVariable (0, 100), //RandomVariable stepProbability
			ConstantVariable (10)); //RandomVariable servicePostErrorDelay

		scrg.GenerateClientsWithRandomFixedDependenceProbabilityToNServices (
			false, // bool deployClientsRandomly,
			numberOfClients, //uint32_t numberOfClients,
			100001, //uint32_t clientBaseId,
			UniformVariable(200, 500), //RandomVariable startTime,
			ConstantVariable(1000000000), //RandomVariable stopTime,
			MilliSeconds(60000), //Time responseTimeout, standard 20 000
			MilliSeconds(1000), //Time ACKTimeout,
			5, //uint32_t retransmissionLimit,
			MilliSeconds(75000), //Time msgIdLifetime, standard 45 000
			clientRequestRate, //RandomVariable planRequestRate,
			UniformVariable(500, 1500), //RandomVariable executionStepRequestSize
			clientRequestRate, //RandomVariable afterFailureWaitingPeriod standard 30000
			numberOfServicesToBeUsedByClients, //uint32_t numberOfServicesToBeUsedByClients
			UniformVariable (0, 100)); //RandomVariable stepProbability

		return scrg;
	}


	static ServiceConfigurationRandomGenerator CreateWithFrontEndBackEndServicesScenario (
			uint32_t numberOfServices,
			RandomVariable numberOfServiceMethods,
			double probabilityOfServiceToServiceConnection,
			uint32_t numberOfClients,
			RandomVariable clientRequestRate,
			uint32_t frontEndServices[],
			uint32_t numberOfFrontEndServices
			)
	{
		NS_LOG_UNCOND("Service layer generation started ...");
		NS_LOG_UNCOND("	Scenario: FrontEndBackEndServices");
		NS_LOG_UNCOND("	Number of services: " << numberOfServices);
		NS_LOG_UNCOND("	Number of service methods: " << numberOfServiceMethods);
		NS_LOG_UNCOND("	Probability of service to service connection: " << probabilityOfServiceToServiceConnection);
		NS_LOG_UNCOND("	Number of clients: " << numberOfClients);
		NS_LOG_UNCOND("	Client request rate: " << clientRequestRate);
		NS_LOG_UNCOND("	Number of services to be used by clients: " << numberOfFrontEndServices);



		Ptr<FaultModel> offFaultModel = CreateObject<SingleRateFaultModel>(false, true, 0, UniformVariable (0., 1.));
		Ptr<FaultModel> serviceFaultModel = CreateObject<OnOffRateFaultModel>(
				false, //bool isEnabled,
				true, //bool isGeneratingException
				false, //bool state,
				0.001, //double offRate,
				UniformVariable (0., 1.), //RandomVariable offRanvar,
				0.1, //double onRate,
				UniformVariable (0., 1.)); //RandomVariable onRanvar

		ServiceConfigurationRandomGenerator scrg;


		scrg.GenerateServices(
			numberOfServices, //uint32_t numberOfServices,
			1, //uint32_t serviceBaseId,
			101, //uint32_t contractBaseId,
			ConstantVariable(0), //RandomVariable numberOfReplicas,
			ConstantVariable(100), //RandomVariable startTime,
			ConstantVariable(1000000000), //RandomVariable stopTime,
			MilliSeconds(60000), //Time responseTimeout, standard 20 000
			MilliSeconds(1000), //Time ACKTimeout,
			5, //uint32_t retransmissionLimit,
			MilliSeconds(75000), //Time msgIdLifetime, standard 45 000
			serviceFaultModel, //Ptr<FaultModel> serviceFaultModel,
			numberOfServiceMethods, //RandomVariable numberOfServiceMethods,
			UniformVariable(500, 1500), //RandomVariable methodResponseSize,
			offFaultModel, //Ptr<FaultModel> methodFaultModel,
			ConstantVariable(20), //RandomVariable methodPreExeDelay,
			ConstantVariable(20), //RandomVariable methodPostExeDelay,
			ConstantVariable (50), //RandomVariable methodPostPlanErrorDelay,
			probabilityOfServiceToServiceConnection, //double executionStepDependencyProbability,
			ConstantVariable(20), //RandomVariable executionStepPostExeDelay,
			UniformVariable(500, 1500), //RandomVariable executionStepRequestSize)
			ConstantVariable (100), //UniformVariable (0, 100), //RandomVariable stepProbability
			ConstantVariable (50)); //RandomVariable servicePostErrorDelay

		scrg.GenerateClientsWithFrontEndBackEndServices (
			false, // bool deployClientsRandomly,
			numberOfClients, //uint32_t numberOfClients,
			100001, //uint32_t clientBaseId,
			UniformVariable(200, 500), //RandomVariable startTime,
			ConstantVariable(1000000000), //RandomVariable stopTime,
			MilliSeconds(60000), //Time responseTimeout, standard 20 000
			MilliSeconds(1000), //Time ACKTimeout,
			5, //uint32_t retransmissionLimit,
			MilliSeconds(75000), //Time msgIdLifetime, standard 45 000
			clientRequestRate, //RandomVariable planRequestRate,
			UniformVariable(500, 1500), //RandomVariable executionStepRequestSize
			clientRequestRate, //RandomVariable afterFailureWaitingPeriod standard 30000
			frontEndServices, //uint32_t [] frontEndServices
			numberOfFrontEndServices, // uint32_t numberOfFrontEndServices
			UniformVariable (0, 100)); //RandomVariable stepProbability

		ServiceRegistry::Initialize(CreateObject<ServiceRegistryServiceSelectorPhysicalDistance>());

		return scrg;
	}


	static ServiceConfigurationRandomGenerator CreateWithSingleServiceDependenceScenario (
			uint32_t numberOfServices,
			RandomVariable numberOfServiceMethods,
			double probabilityOfServiceToServiceConnection,
			uint32_t numberOfClients,
			RandomVariable clientRequestRate,
			uint32_t singleServiceId)
	{
		NS_LOG_UNCOND("Service layer generation started ...");
		NS_LOG_UNCOND("	Scenario: WithSingleServiceDependence");
		NS_LOG_UNCOND("	Number of services: " << numberOfServices);
		NS_LOG_UNCOND("	Number of service methods: " << numberOfServiceMethods);
		NS_LOG_UNCOND("	Probability of service to service connection: " << probabilityOfServiceToServiceConnection);
		NS_LOG_UNCOND("	Number of clients: " << numberOfClients);
		NS_LOG_UNCOND("	Client request rate: " << clientRequestRate);
		NS_LOG_UNCOND("	Single service Id: " << singleServiceId);


		Ptr<FaultModel> offFaultModel = CreateObject<SingleRateFaultModel>(false, true, 0, UniformVariable (0., 1.));
		Ptr<FaultModel> serviceFaultModel = CreateObject<OnOffRateFaultModel>(
				false, //bool isEnabled,
				true, //bool isGeneratingException
				false, //bool state,
				0.001, //double offRate,
				UniformVariable (0., 1.), //RandomVariable offRanvar,
				0.1, //double onRate,
				UniformVariable (0., 1.)); //RandomVariable onRanvar

		ServiceConfigurationRandomGenerator scrg;


		scrg.GenerateServices(
			numberOfServices, //uint32_t numberOfServices,
			1, //uint32_t serviceBaseId,
			1, //uint32_t contractBaseId,
			ConstantVariable(2), //RandomVariable numberOfReplicas,
			ConstantVariable(100), //RandomVariable startTime,
			ConstantVariable(1000000000), //RandomVariable stopTime,
			MilliSeconds(60000), //Time responseTimeout, standard 20 000
			MilliSeconds(1000), //Time ACKTimeout,
			5, //uint32_t retransmissionLimit,
			MilliSeconds(75000), //Time msgIdLifetime, standard 45 000
			serviceFaultModel, //Ptr<FaultModel> serviceFaultModel,
			numberOfServiceMethods, //RandomVariable numberOfServiceMethods,
			UniformVariable(500, 1500), //RandomVariable methodResponseSize,
			offFaultModel, //Ptr<FaultModel> methodFaultModel,
			ConstantVariable(20), //RandomVariable methodPreExeDelay,
			ConstantVariable(20), //RandomVariable methodPostExeDelay,
			ConstantVariable (10), //RandomVariable methodPostPlanErrorDelay,
			probabilityOfServiceToServiceConnection, //double executionStepDependencyProbability,
			ConstantVariable(10), //RandomVariable executionStepPostExeDelay,
			UniformVariable(500, 1500), //RandomVariable executionStepRequestSize)
			UniformVariable (0, 100), //RandomVariable stepProbability
			ConstantVariable (10)); //RandomVariable servicePostErrorDelay

		scrg.GenerateClientsWithSingleServiceDependence (
			false, // bool deployClientsRandomly,
			numberOfClients, //uint32_t numberOfClients,
			100001, //uint32_t clientBaseId,
			UniformVariable(200, 500), //RandomVariable startTime,
			ConstantVariable(1000000000), //RandomVariable stopTime,
			MilliSeconds(60000), //Time responseTimeout, standard 20 000
			MilliSeconds(1000), //Time ACKTimeout,
			5, //uint32_t retransmissionLimit,
			MilliSeconds(75000), //Time msgIdLifetime, standard 45 000
			clientRequestRate, //RandomVariable planRequestRate,
			UniformVariable(500, 1500), //RandomVariable executionStepRequestSize
			clientRequestRate, //RandomVariable afterFailureWaitingPeriod standard 30000
			singleServiceId); //uint32_t singleServiceId

		return scrg;
	}

/*
	static ServiceConfigurationRandomGenerator CreateNtoNFullDependence (uint32_t numberOfClients, uint32_t numberOfServices)
	{
		Ptr<FaultModel> on = CreateObject<FaultModel>(0, UniformVariable (0., 1.));
		Ptr<FaultModel> off = CreateObject<FaultModel>(0, UniformVariable (0., 1.));

		ServiceConfigurationRandomGenerator scrg;

		scrg.GenerateServices(
			  numberOfServices, //uint32_t numberOfServices,
			  1, //uint32_t serviceBaseId,
			  1001, //uint32_t contractBaseId,
			  ConstantVariable(1000), //RandomVariable startTime,
			  ConstantVariable(100000), //RandomVariable stopTime,
			  off, //Ptr<FaultModel> serviceFaultModel,
			  ConstantVariable(2), //RandomVariable numberOfServiceMethods,
			  ConstantVariable(500), //RandomVariable methodResponseSize,
			  off, //Ptr<FaultModel> methodFaultModel,
			  ConstantVariable(10), //RandomVariable methodPreExeDelay,
			  ConstantVariable(20), //RandomVariable methodPostExeDelay,
			  MilliSeconds(5000), //Time methodResponseTimeout,
			  1, //double executionStepDependencyProbability,
			  ConstantVariable(10), //RandomVariable executionStepPostExeDelay,
			  ConstantVariable(300)); //RandomVariable executionStepRequestSize)

		scrg.GenerateClients (
			numberOfClients, //uint32_t numberOfClients,
			100001, //uint32_t clientBaseId,
			UniformVariable(2000, 5000), //RandomVariable startTime,
			ConstantVariable(100000), //RandomVariable stopTime,
			ConstantVariable(100), //RandomVariable clientPreExeDelay,
			ConstantVariable(5000), //RandomVariable clientPostExeDelay,
			MilliSeconds(5000), //Time responseTimeout,
			1, //double executionStepDependencyProbability,
			ConstantVariable(2000), //RandomVariable executionStepPostExeDelay,
			ConstantVariable(300)); //RandomVariable executionStepRequestSize)

		return scrg;
	}

	static ServiceConfigurationRandomGenerator Create10to20 ()
	{
		Ptr<FaultModel> on = CreateObject<FaultModel>(0, UniformVariable (0., 1.));
		Ptr<FaultModel> off = CreateObject<FaultModel>(0, UniformVariable (0., 1.));

		ServiceConfigurationRandomGenerator scrg;

		  scrg.GenerateServices(
				  20, //uint32_t numberOfServices,
				  1, //uint32_t serviceBaseId,
				  1001, //uint32_t contractBaseId,
				  ConstantVariable(1000), //RandomVariable startTime,
				  ConstantVariable(100000), //RandomVariable stopTime,
				  off, //Ptr<FaultModel> serviceFaultModel,
				  ConstantVariable(3), //RandomVariable numberOfServiceMethods,
				  ConstantVariable(500), //RandomVariable methodResponseSize,
				  off, //Ptr<FaultModel> methodFaultModel,
				  ConstantVariable(10), //RandomVariable methodPreExeDelay,
				  ConstantVariable(20), //RandomVariable methodPostExeDelay,
				  MilliSeconds(10000), //Time methodResponseTimeout,
				  0.03, //double executionStepDependencyProbability,
				  ConstantVariable(10), //RandomVariable executionStepPostExeDelay,
				  ConstantVariable(300)); //RandomVariable executionStepRequestSize)

			scrg.GenerateClients (
					10, //uint32_t numberOfClients,
					100001, //uint32_t clientBaseId,
					UniformVariable(2000, 3000), //RandomVariable startTime,
					ConstantVariable(100000), //RandomVariable stopTime,
					ConstantVariable(100), //RandomVariable clientPreExeDelay,
					ConstantVariable(100), //RandomVariable clientPostExeDelay,
					MilliSeconds(10000), //Time responseTimeout,
					0.1, //double executionStepDependencyProbability,
					ConstantVariable(10), //RandomVariable executionStepPostExeDelay,
					ConstantVariable(300)); //RandomVariable executionStepRequestSize)

		return scrg;
	}

	static ServiceConfigurationRandomGenerator Create100to100 ()
	{
		Ptr<FaultModel> on = CreateObject<FaultModel>(0, UniformVariable (0., 1.));
		Ptr<FaultModel> off = CreateObject<FaultModel>(0, UniformVariable (0., 1.));

		ServiceConfigurationRandomGenerator scrg;

		  scrg.GenerateServices(
				  100, //uint32_t numberOfServices,
				  1, //uint32_t serviceBaseId,
				  1001, //uint32_t contractBaseId,
				  ConstantVariable(1000), //RandomVariable startTime,
				  ConstantVariable(100000), //RandomVariable stopTime,
				  off, //Ptr<FaultModel> serviceFaultModel,
				  ConstantVariable(3), //RandomVariable numberOfServiceMethods,
				  ConstantVariable(500), //RandomVariable methodResponseSize,
				  off, //Ptr<FaultModel> methodFaultModel,
				  ConstantVariable(10), //RandomVariable methodPreExeDelay,
				  ConstantVariable(20), //RandomVariable methodPostExeDelay,
				  MilliSeconds(5000), //Time methodResponseTimeout,
				  0.03, //double executionStepDependencyProbability,
				  ConstantVariable(10), //RandomVariable executionStepPostExeDelay,
				  ConstantVariable(300)); //RandomVariable executionStepRequestSize)

			scrg.GenerateClients (
					100, //uint32_t numberOfClients,
					100001, //uint32_t clientBaseId,
					UniformVariable(2000, 3000), //RandomVariable startTime,
					ConstantVariable(100000), //RandomVariable stopTime,
					ConstantVariable(100), //RandomVariable clientPreExeDelay,
					ConstantVariable(100), //RandomVariable clientPostExeDelay,
					MilliSeconds(5000), //Time responseTimeout,
					0.2, //double executionStepDependencyProbability,
					ConstantVariable(10), //RandomVariable executionStepPostExeDelay,
					ConstantVariable(300)); //RandomVariable executionStepRequestSize)

		return scrg;
	}

	static ServiceConfigurationRandomGenerator CreateNtoNRandomDependence (uint32_t numberOfClients, uint32_t numberOfServices)
	{
		Ptr<FaultModel> on = CreateObject<FaultModel>(0, UniformVariable (0., 1.));
		Ptr<FaultModel> off = CreateObject<FaultModel>(0, UniformVariable (0., 1.));

		ServiceConfigurationRandomGenerator scrg;

		  scrg.GenerateServices(
				  numberOfServices, //uint32_t numberOfServices,
				  1, //uint32_t serviceBaseId,
				  1001, //uint32_t contractBaseId,
				  ConstantVariable(100), //RandomVariable startTime,
				  ConstantVariable(1000000), //RandomVariable stopTime,
				  off, //Ptr<FaultModel> serviceFaultModel,
				  UniformVariable(3, 5), //RandomVariable numberOfServiceMethods,
				  UniformVariable(200, 500), //RandomVariable methodResponseSize,
				  off, //Ptr<FaultModel> methodFaultModel,
				  ConstantVariable(10), //RandomVariable methodPreExeDelay,
				  ConstantVariable(20), //RandomVariable methodPostExeDelay,
				  MilliSeconds(5000), //Time methodResponseTimeout,
				  0.01, //double executionStepDependencyProbability,
				  UniformVariable(10, 100), //RandomVariable executionStepPostExeDelay,
				  UniformVariable(200, 500)); //RandomVariable executionStepRequestSize)

			scrg.GenerateClients (
					numberOfClients, //uint32_t numberOfClients,
					100001, //uint32_t clientBaseId,
					UniformVariable(2000, 5000), //RandomVariable startTime,
					ConstantVariable(1000000), //RandomVariable stopTime,
					ConstantVariable(100), //RandomVariable clientPreExeDelay,
					ConstantVariable(5000), //RandomVariable clientPostExeDelay,
					MilliSeconds(5000), //Time responseTimeout,
					0.015, //double executionStepDependencyProbability,
					UniformVariable(500, 2000), //RandomVariable executionStepPostExeDelay,
					UniformVariable(200, 500)); //RandomVariable executionStepRequestSize)

		return scrg;
	}
*/

}; // ServiceConfigurationGeneratorFactory





class ScenarioSimulation
{
private:
	const NodeContainer 				m_nodes;
	const Ptr<ServiceConfiguration> 	m_serviceConfiguration;
	Ptr<SimulationOutput>				m_simulationOutput;
	struct timeval 						m_simulationStartTime;
	const NodeAssignment * 				m_fixedNodeAssignments;
	const uint32_t 						m_fixedNodeAssignmentsSize;


public:

	ScenarioSimulation (
			NodeContainer nodes,
			Ptr<ServiceConfiguration> serviceConfiguration,
			NodeAssignment * fixedNodeAssignments,
			uint32_t fixedNodeAssignmentsSize)
	:m_nodes(nodes),
	 m_serviceConfiguration(serviceConfiguration),
	 m_fixedNodeAssignments(fixedNodeAssignments),
	 m_fixedNodeAssignmentsSize(fixedNodeAssignmentsSize)
	{
		m_simulationOutput = CreateObject<SimulationOutput>("msg.csv", "err.csv", "rtable.txt");
	}

	virtual ~ScenarioSimulation () {}

	void RunSimulation (
			Time simulationRunLength,
			bool writeOutServiceConfigurationStatistics,
			bool writeOutGraphProperties,
			bool writeOutSimulationLoadingInfo,
			bool writeOutSimulationRunStatistics,
			bool writeOutServiceRegistryEndState,
			bool writeOutSimulationTimeProgress)
	{
		LoadSimulation(
				writeOutServiceConfigurationStatistics,
				writeOutGraphProperties,
				writeOutSimulationLoadingInfo);

		NS_LOG_UNCOND("----------------------------------------------------------------");
		NS_LOG_UNCOND("Starting scenario simulation ...");
		NS_LOG_UNCOND("	Simulation run length: " << simulationRunLength.GetSeconds() << "s");
		NS_LOG_UNCOND("	... this may take some time ...");

		InitializeSimulationStartTime();

		if (writeOutSimulationTimeProgress)
		{
			WriteOutSimulationTimingOutput();
		}

		Simulator::Stop(simulationRunLength);
		Simulator::Run();
		Simulator::Destroy ();

		NS_LOG_UNCOND("Simulation finished successfully");
		NS_LOG_UNCOND("----------------------------------------------------------------");

		if (writeOutSimulationRunStatistics)
		{
			WriteOutSimulationRunStatistics();
		}

		if (writeOutServiceRegistryEndState)
		{
			ServiceRegistry::WriteOut();
		}


		NS_LOG_UNCOND("----------------------------------------------------------------");
		NS_LOG_UNCOND("	Simulation elapsed real time: " << GetSimulationTimeElapsed() << "s");
		NS_LOG_UNCOND("----------------------------------------------------------------");

		m_simulationOutput->Flush();
	}

private:

	void InitializeSimulationStartTime()
	{
		gettimeofday(&m_simulationStartTime, NULL);
	}

	long GetSimulationTimeElapsed()
	{
	    struct timeval end;
		long mtime, seconds;//, useconds;


		gettimeofday(&end, NULL);
		seconds  = end.tv_sec  - m_simulationStartTime.tv_sec;
		//useconds = end.tv_usec - m_simulationStartTime.tv_usec;
		mtime = seconds;//((seconds) * 1000 + useconds/1000.0) + 0.5;

		return mtime;
	}

	void WriteOutSimulationTimingOutput ()
	{
		NS_LOG_UNCOND("	Simulation time: " << Simulator::Now().GetSeconds() << "s - elapsed real time: " << GetSimulationTimeElapsed() << "s");
		//InstanceCounter::WriteOut();
		Simulator::Schedule (MilliSeconds(1000), &ScenarioSimulation::WriteOutSimulationTimingOutput, this);
	}

	void LoadSimulation (
			bool writeOutServiceConfigurationStatistics,
			bool writeOutGraphProperties,
			bool writeOutSimulationLoadingInfo)
	{
		if (writeOutServiceConfigurationStatistics)
		{
			m_serviceConfiguration->WriteOutStatistics();
		}

		if(writeOutGraphProperties)
		{
			m_serviceConfiguration->WriteOutGraphProperties();
		}

		SimulationLoader simulationLoader(
				m_nodes, // NodeContainer nodes,
				m_simulationOutput, //Ptr<SimulationOutput> simulationOutput,
				m_serviceConfiguration, // Ptr<ServiceConfiguration> serviceConfiguration,
				10000, // uint16_t servicePortBaseId
				m_fixedNodeAssignments, // NodeAssignment[] fixedNodeAssignments,
				m_fixedNodeAssignmentsSize, // uint32_t fixedNodeAssignmentsSize,
				writeOutSimulationLoadingInfo); // bool writeOut

		simulationLoader.LoadServiceConfiguration();
	}

	void WriteOutSimulationRunStatistics()
	{
		NS_LOG_UNCOND("Simulation execution statistics");
		NS_LOG_UNCOND("		Note: some messages may have not arrived due to the end of the simulation");

		NS_LOG_UNCOND("	Message layer ...");
		NS_LOG_UNCOND("		Total number of unique messages: " << Message::GetMessageCounter());
		NS_LOG_UNCOND("		Number of conversations: " << Message::GetConversationCounter());

		NS_LOG_UNCOND("		Requests---------------------------------------");
		NS_LOG_UNCOND("		Unique: " << MessageEndpoint::GetMessageCounter(0).msgSendUniqueCounter);
		NS_LOG_UNCOND("		Send attempts: " << MessageEndpoint::GetMessageCounter(0).msgSendAttemptCounter);
		NS_LOG_UNCOND("			Resend attempts: " << MessageEndpoint::GetMessageCounter(0).msgSendAttemptCounter - MessageEndpoint::GetMessageCounter(0).msgSendUniqueCounter);
		NS_LOG_UNCOND("			Failures on sockets: " << MessageEndpoint::GetMessageCounter(0).msgSendAttemptCounter - MessageEndpoint::GetMessageCounter(0).msgSendSuccessCounter);
		NS_LOG_UNCOND("			Sent successfully on sockets: " << MessageEndpoint::GetMessageCounter(0).msgSendSuccessCounter);
		NS_LOG_UNCOND("			ACK timeouts: " << MessageEndpoint::GetMessageCounter(0).msgACKTimeoutCounter);
		NS_LOG_UNCOND("			Requests receiving ACK: " << MessageEndpoint::GetMessageCounter(0).msgSendAttemptCounter - MessageEndpoint::GetMessageCounter(0).msgACKTimeoutCounter);
		NS_LOG_UNCOND("			Send failure (5 attempts failed): " << MessageEndpoint::GetMessageCounter(0).msgSendFailureCounter);
		NS_LOG_UNCOND("		Received: " << MessageEndpoint::GetMessageCounter(0).msgReceiveCounter);
		NS_LOG_UNCOND("			Unique: " << MessageEndpoint::GetMessageCounter(0).msgReceiveUniqueCounter);
		NS_LOG_UNCOND("			Dropped (due to resend): " << MessageEndpoint::GetMessageCounter(0).msgReceiveCounter - MessageEndpoint::GetMessageCounter(0).msgReceiveUniqueCounter);
		NS_LOG_UNCOND("		Response timeouts: " << MessageEndpoint::GetMessageCounter(0).msgResponseTimeoutCounter);

		NS_LOG_UNCOND("		Responses---------------------------------------");
		NS_LOG_UNCOND("		Unique: " << MessageEndpoint::GetMessageCounter(1).msgSendUniqueCounter);
		NS_LOG_UNCOND("		Send attempts: " << MessageEndpoint::GetMessageCounter(1).msgSendAttemptCounter);
		NS_LOG_UNCOND("			Resend attempts: " << MessageEndpoint::GetMessageCounter(1).msgSendAttemptCounter - MessageEndpoint::GetMessageCounter(1).msgSendUniqueCounter);
		NS_LOG_UNCOND("			Failures on sockets: " << MessageEndpoint::GetMessageCounter(1).msgSendAttemptCounter - MessageEndpoint::GetMessageCounter(1).msgSendSuccessCounter);
		NS_LOG_UNCOND("			Sent successfully on sockets: " << MessageEndpoint::GetMessageCounter(1).msgSendSuccessCounter);
		NS_LOG_UNCOND("			ACK timeouts: " << MessageEndpoint::GetMessageCounter(1).msgACKTimeoutCounter);
		NS_LOG_UNCOND("			Responses receiving ACK: " << MessageEndpoint::GetMessageCounter(1).msgSendAttemptCounter - MessageEndpoint::GetMessageCounter(1).msgACKTimeoutCounter);
		NS_LOG_UNCOND("			Send failure (5 attempts failed): " << MessageEndpoint::GetMessageCounter(1).msgSendFailureCounter);
		NS_LOG_UNCOND("		Received: " << MessageEndpoint::GetMessageCounter(1).msgReceiveCounter);
		NS_LOG_UNCOND("			Unique: " << MessageEndpoint::GetMessageCounter(1).msgReceiveUniqueCounter);
		NS_LOG_UNCOND("			Dropped (due to resend): " << MessageEndpoint::GetMessageCounter(1).msgReceiveCounter - MessageEndpoint::GetMessageCounter(1).msgReceiveUniqueCounter);

		NS_LOG_UNCOND("		Responses exceptions ---------------------------------------");
		NS_LOG_UNCOND("		Unique: " << MessageEndpoint::GetMessageCounter(2).msgSendUniqueCounter);
		NS_LOG_UNCOND("		Send attempts: " << MessageEndpoint::GetMessageCounter(2).msgSendAttemptCounter);
		NS_LOG_UNCOND("			Resend attempts: " << MessageEndpoint::GetMessageCounter(2).msgSendAttemptCounter - MessageEndpoint::GetMessageCounter(2).msgSendUniqueCounter);
		NS_LOG_UNCOND("			Failures on sockets: " << MessageEndpoint::GetMessageCounter(2).msgSendAttemptCounter - MessageEndpoint::GetMessageCounter(2).msgSendSuccessCounter);
		NS_LOG_UNCOND("			Sent successfully on sockets: " << MessageEndpoint::GetMessageCounter(2).msgSendSuccessCounter);
		NS_LOG_UNCOND("			ACK timeouts: " << MessageEndpoint::GetMessageCounter(2).msgACKTimeoutCounter);
		NS_LOG_UNCOND("			Responses receiving ACK: " << MessageEndpoint::GetMessageCounter(2).msgSendAttemptCounter - MessageEndpoint::GetMessageCounter(2).msgACKTimeoutCounter);
		NS_LOG_UNCOND("			Send failure (5 attempts failed): " << MessageEndpoint::GetMessageCounter(2).msgSendFailureCounter);
		NS_LOG_UNCOND("		Received: " << MessageEndpoint::GetMessageCounter(2).msgReceiveCounter);
		NS_LOG_UNCOND("			Unique: " << MessageEndpoint::GetMessageCounter(2).msgReceiveUniqueCounter);
		NS_LOG_UNCOND("			Dropped (due to resend): " << MessageEndpoint::GetMessageCounter(2).msgReceiveCounter - MessageEndpoint::GetMessageCounter(2).msgReceiveUniqueCounter);

		MessageEndpoint::MessageTypeCounter rt = MessageEndpoint::GetMessageCounter(1);
		MessageEndpoint::MessageTypeCounter rtc = MessageEndpoint::GetMessageCounter(2);

		rt.msgSendAttemptCounter += rtc.msgSendAttemptCounter;
		rt.msgSendSuccessCounter += rtc.msgSendSuccessCounter;
		rt.msgSendUniqueCounter += rtc.msgSendUniqueCounter;
		rt.msgReceiveCounter += rtc.msgReceiveCounter;
		rt.msgReceiveUniqueCounter += rtc.msgReceiveUniqueCounter;
		rt.msgSendFailureCounter += rtc.msgSendFailureCounter;
		rt.msgACKTimeoutCounter += rtc.msgACKTimeoutCounter;
		rt.msgResponseTimeoutCounter += rtc.msgResponseTimeoutCounter;

		NS_LOG_UNCOND("		Responses total ---------------------------------------");
		NS_LOG_UNCOND("		Unique: " << rt.msgSendUniqueCounter);
		NS_LOG_UNCOND("		Send attempts: " << rt.msgSendAttemptCounter);
		NS_LOG_UNCOND("			Resend attempts: " << rt.msgSendAttemptCounter - rt.msgSendUniqueCounter);
		NS_LOG_UNCOND("			Failures on sockets: " << rt.msgSendAttemptCounter - rt.msgSendSuccessCounter);
		NS_LOG_UNCOND("			Sent successfully on sockets: " << rt.msgSendSuccessCounter);
		NS_LOG_UNCOND("			ACK timeouts: " << rt.msgACKTimeoutCounter);
		NS_LOG_UNCOND("			Responses receiving ACK: " << rt.msgSendAttemptCounter - rt.msgACKTimeoutCounter);
		NS_LOG_UNCOND("			Send failure (5 attempts failed): " << rt.msgSendFailureCounter);
		NS_LOG_UNCOND("		Received: " << rt.msgReceiveCounter);
		NS_LOG_UNCOND("			Unique: " << rt.msgReceiveUniqueCounter);
		NS_LOG_UNCOND("			Dropped (due to resend): " << rt.msgReceiveCounter - rt.msgReceiveUniqueCounter);

		NS_LOG_UNCOND("		ACK ---------------------------------------");
		NS_LOG_UNCOND("		ACK: " << MessageEndpoint::GetMessageCounter(3).msgSendUniqueCounter);
		NS_LOG_UNCOND("			Failures on sockets: " << MessageEndpoint::GetMessageCounter(3).msgSendAttemptCounter - MessageEndpoint::GetMessageCounter(3).msgSendSuccessCounter);
		NS_LOG_UNCOND("			Sent successfully on sockets: " << MessageEndpoint::GetMessageCounter(3).msgSendSuccessCounter);
		NS_LOG_UNCOND("		Received: " << MessageEndpoint::GetMessageCounter(3).msgReceiveCounter);

		NS_LOG_UNCOND("	Service layer ...");
		NS_LOG_UNCOND("		Service - number of received requests: " << ServiceInstance::GetNumberOfServiceRequests());
		NS_LOG_UNCOND("		Service - number of service failures: " << ServiceRequestTask::GetNumberOfServiceFailures());
		NS_LOG_UNCOND("		Service method - number of started methods: " << ServiceRequestTask::GetNumberOfStartedMethods());
		NS_LOG_UNCOND("		Service method - number of failed methods: " << ServiceRequestTask::GetNumberOfFailedMethods());
		NS_LOG_UNCOND("		Service method - number of failed methods (including fault propagation): " << ServiceRequestTask::GetNumberOfFailedExecutions());
		NS_LOG_UNCOND("		Service - number of issued exception response messages: " << ServiceRequestTask::GetNumberOfIssuedExceptionMessages());
		NS_LOG_UNCOND("	Simulation ...");
		NS_LOG_UNCOND("		Total number of all symptoms (including ACK timeouts etc): " << SimulationOutput::GetErrCounter());
	}

}; // ScenarioSimulation



NS_LOG_COMPONENT_DEFINE ("service_scenario");







void runSimulationHybrid()
{

	// network configuration
	AdHocMobileNetworkConfigurationGenerator networkGenerator = AdHocMobileNetworkConfigurationGenerator();
	NodeContainer nodes;


	// hybrid
	networkGenerator.GenerateNetworkHybrid (
			7, //numberOfMobileNodes,
			3, //numberOfStaticNodes,
			750, //gridXLength,
			750, //gridYLength,
			300, //gridXLengthModifierForStaticNodes,
			300, //gridYLengthModifierForStaticNodes,
			1.388, //double mobilitySpeed)    1.388m/s =5k/h
			ConstantVariable (0.0)); // mobility pause

	nodes = networkGenerator.GetNodes();


	uint32_t frontEndServices [3] = { 1, 3, 5};

	ServiceConfigurationRandomGenerator serviceGenerator = ServiceConfigurationGeneratorFactory::CreateWithFrontEndBackEndServicesScenario(
			11, //uint32_t numberOfServices,
			ConstantVariable(1), //RandomVariable numberOfServiceMethods,
			0.4,//0.05, 0.025, 0.0125 //double probabilityOfServiceToServiceConnection,
			5, //uint32_t numberOfClients,
			UniformVariable(2500, 7500), //RandomVariable clientRequestRate);
			frontEndServices, //uint32_t [] frontEndServices
			sizeof(frontEndServices)/sizeof(uint32_t)); //); //uint32_t numberOfFrontEndServices

	Ptr<ServiceConfiguration> serviceConfiguration = serviceGenerator.GetServiceConfiguration();

	// number of services has to be higher in order to compensate for orphans

	// client and service assignment to nodes (nodeId, serviceId)

	// nodes - 0-6 mobile 7-9 static
	// services 1-3 f 4-10 b
	// clients 100001-100005

	// 1 client on static rest on mobile
	// 1 front 1 back on static rest on mobile

	NodeAssignment nodeAssignments [] = {{7, 100005},
			/*fronts - 1 on static*/ {8, 1}, {3, 2}, {6, 3},
			/*backs - 1 on static*/ {0, 4}, {1, 5}, {2, 6}, {3, 7}, {4, 8}, {5, 9}, {9, 10}};
	uint32_t nodeAssignmentsSize = 11;


	// simulation
	ScenarioSimulation scenarioSimulation(
			nodes,
			serviceConfiguration,
			nodeAssignments,
			nodeAssignmentsSize);

	scenarioSimulation.RunSimulation(
		Seconds(1860), // Time simulationRunLength,
		true, // bool writeOutServiceConfigurationStatistics,
		false, // bool writeOutGraphProperties
		false, // bool writeOutSimulationLoadingInfo,
		true, // bool writeOutSimulationRunStatistics,
		false, //bool writeOutServiceRegistryEndState
		true); // bool writeOutSimulationTimeProgress

}


int main (int argc, char *argv[])
{

	/*
	// example of hybrid wireless network with 10 nodes
	runSimulationHybrid();
	return 0;
	*/



	// example experiment: MANET with 50 nodes and 30 services

	// network configuration
	AdHocMobileNetworkConfigurationGenerator networkGenerator = AdHocMobileNetworkConfigurationGenerator();
	NodeContainer nodes;



	// MANET
	networkGenerator.GenerateNetworkMANET(
		50, //uint32_t numberOfNodes,
		125, //uint32_t gridXLength,
		125, //uint32_t gridYLength,
		2); //double mobilitySpeed)

	nodes = networkGenerator.GetNodes();


	// service configuration - WithFrontEndBackEndServices

	uint32_t frontEndServices [5] = { 1, 4, 7, 10, 13};

	ServiceConfigurationRandomGenerator serviceGenerator = ServiceConfigurationGeneratorFactory::CreateWithFrontEndBackEndServicesScenario(
			30, //uint32_t numberOfServices,
			ConstantVariable(2), //RandomVariable numberOfServiceMethods,
			0.025,//0.05, 0.025, 0.0125 //double probabilityOfServiceToServiceConnection,
			50, //uint32_t numberOfClients,
			UniformVariable(5000, 15000), //ExponentialVariable(20000)); //RandomVariable clientRequestRate);
			frontEndServices, //uint32_t [] frontEndServices
			sizeof(frontEndServices)/sizeof(uint32_t)); //); //uint32_t numberOfFrontEndServices


	Ptr<ServiceConfiguration> serviceConfiguration = serviceGenerator.GetServiceConfiguration();

	// setting of fault models if needed
	//Ptr<AbsoluteTimeFaultModel>	abstime = CreateObject<AbsoluteTimeFaultModel>(true, true, MilliSeconds(8000), MilliSeconds(10000));
	//Ptr<OnOffTimeFaultModel>	oftime = CreateObject<OnOffTimeFaultModel>(true, true, false, ConstantVariable(2000), ConstantVariable(500));
	//Ptr<CompositeFaultModel>	comtime = CreateObject<CompositeFaultModel>(true);

	//	comtime->AddFaultModel(abstime);
	//	comtime->AddFaultModel(oftime);

	//serviceConfiguration->GetService(30)->SetFaultModel(abstime);
	//serviceConfiguration->GetService(40)->SetFaultModel(oftime);
	//serviceConfiguration->GetService(50)->SetFaultModel(comtime);
	//serviceConfiguration->GetService(40)->SetFaultModel(oftime);

	//serviceConfiguration->GetService(50)->SetFaultModel(oftime);



	// client and service assignment to nodes
	NodeAssignment nodeAssignments [] = {{1, 2}, {1, 2}, {1, 2}};
	uint32_t nodeAssignmentsSize = 0;

	// simulation
	ScenarioSimulation scenarioSimulation(
			nodes,
			serviceConfiguration,
			nodeAssignments,
			nodeAssignmentsSize);


	//LogComponentEnableAll(NS_LOG_ERROR);

	scenarioSimulation.RunSimulation(
		Seconds(1860), // Time simulationRunLength,
		true, // bool writeOutServiceConfigurationStatistics,
		false, // bool writeOutGraphProperties
		false, // bool writeOutSimulationLoadingInfo,
		true, // bool writeOutSimulationRunStatistics,
		false, //bool writeOutServiceRegistryEndState
		true); // bool writeOutSimulationTimeProgress

  return 0;
}
