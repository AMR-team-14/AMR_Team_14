// Local evaluator for the published IE325 case. The dispatch loop deliberately
// checks every requested candidate instead of reproducing C++ queue pruning.
export function buildCase(data) {
  const n = data.nodes.length;
  const adj = Array.from({length:n}, () => []);
  const degree = Array(n).fill(0);
  for (const [i, [from, to, distance]] of data.edges.entries()) {
    const [a,b] = data.fixedDir[i] === "0" ? [from,to] : [to,from];
    adj[a].push([b,distance]);
    degree[a]++;
    degree[b]++;
  }
  const intersections = degree.map(value => value >= 3);
  const distances = [];
  const routes = [];
  const scale = 60 / data.at;
  for (let start=0; start<n; start++) {
    const distance = Array(n).fill(Infinity);
    const previous = Array(n).fill(-1);
    const settled = Array(n).fill(false);
    distance[start] = 0;
    for (let i=0; i<n; i++) {
      let best = -1;
      for (let j=0; j<n; j++) {
        if (!settled[j] && (best === -1 || distance[j] < distance[best])) best = j;
      }
      if (!Number.isFinite(distance[best])) throw new Error("공식 통로 데이터에서 도달할 수 없는 지점이 있습니다.");
      settled[best] = true;
      for (const [target, length] of adj[best]) {
        if (distance[best] + length < distance[target]) {
          distance[target] = distance[best] + length;
          previous[target] = best;
        }
      }
    }
    distances.push(distance);
    routes.push(previous.map((_,target) => {
      const path = [];
      for (let at=target; at!==start; at=previous[at]) path.push(at);
      path.reverse();
      return [data.ve/scale, data.vc/scale].map(speed => {
        let segment = 0, current = start;
        const hops = [];
        for (const next of path) {
          segment += adj[current].find(([to]) => to === next)[1] / speed;
          if (intersections[next] || next === target) {
            hops.push([next,segment]);
            segment = 0;
          }
          current = next;
        }
        return hops;
      });
    }));
  }
  return {...data, intersections, distances, routes,
    load:data.tl*scale, unload:data.tu*scale, crossing:data.p*scale};
}

export function decodeReference(model, packed) {
  let millis = 0;
  return packed.split(" ").map(token => {
    const value = Number.parseInt(token,36);
    millis += Math.floor(value/model.flows.length);
    const [from,to] = model.flows[value % model.flows.length];
    return {at:millis/1000, from:model.pd[from], to:model.pd[to], urgent:Number(to===model.urgentDest)};
  });
}

export function generateArrivals(model, seed) {
  let state = seed;
  function random() {
    state = (state + 0x6D2B79F5) | 0;
    let t = Math.imul(state ^ (state >>> 15), state | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  }
  const flow = model.flows.reduce((sum, row) => sum + row[2], 0);
  let probability = 0;
  const bounds = model.flows.map(row => probability += row[2] / flow);
  const lambda = flow * model.alpha / 60;
  const arrivals = [];
  for (let time=0;;) {
    time += -Math.log(Math.max(1e-12,random())) / lambda;
    if (time > model.T) return arrivals;
    const draw = random();
    const index = bounds.findIndex(value => value >= draw);
    const [from,to] = model.flows[index < 0 ? bounds.length-1 : index];
    arrivals.push({at:time, from:model.pd[from], to:model.pd[to], urgent:Number(to === model.urgentDest)});
  }
}

const round = time => Math.round(time * 1e9) / 1e9;
const before = (a,b) => a.time < b.time || (a.time === b.time &&
  (a.kind < b.kind || (a.kind === b.kind && a.id < b.id)));
const REQUEST = 0, REACH = 1, LOADED = 2, UNLOADED = 3;

class Events {
  items = [];

  add(time,kind,id) {
    const event = {time:round(time),kind,id};
    let i = this.items.length;
    this.items.push(event);
    while (i > 0) {
      const parent = (i-1) >> 1;
      if (!before(event,this.items[parent])) break;
      this.items[i] = this.items[parent];
      i = parent;
    }
    this.items[i] = event;
  }

  peek() { return this.items[0]; }

  pop() {
    const first = this.peek();
    const last = this.items.pop();
    if (!this.items.length) return first;
    let i = 0;
    while (2*i+1 < this.items.length) {
      let child = 2*i+1;
      if (child+1 < this.items.length && before(this.items[child+1],this.items[child])) child++;
      if (!before(this.items[child],last)) break;
      this.items[i] = this.items[child];
      i = child;
    }
    this.items[i] = last;
    return first;
  }
}

export function simulate(model, policy, arrivals, onDispatch) {
  const events = new Events();
  const vehicles = Array.from({length:policy.fleet}, (_,id) =>
    ({id, position:model.pd[id % model.pd.length], idle:true}));
  const freeAt = new Float64Array(model.nodes.length);
  const waitingAt = new Int32Array(model.nodes.length);
  const pickup = new Float64Array(arrivals.length).fill(-1);
  const queue = [];
  let maxQueue = 0;
  for (const [id, request] of arrivals.entries()) events.add(request.at,REQUEST,id);

  function stop(time,car) {
    if (!car.loaded) pickup[car.requestId] = time;
    events.add(time + (car.loaded ? model.unload : model.load),
      car.loaded ? UNLOADED : LOADED,car.id);
  }

  function travel(time,car,target,loaded) {
    car.loaded = loaded;
    const route = model.routes[car.position][target][Number(loaded)];
    if (!route.length) { stop(time,car); return; }
    car.route = route;
    car.hop = 0;
    events.add(time + route[0][1],REACH,car.id);
  }

  function priority(request,time,de) {
    let bits = 0, place = 16;
    for (let id=1; id!==0; place >>= 1) {
      const rule = policy.nodes.get(id);
      let value;
      switch (rule.feature) {
        case "urg": value = request.urgent; break;
        case "w": value = time-request.at; break;
        case "de": value = de; break;
        case "dl": value = model.distances[request.from][request.to]; break;
        case "qo": value = waitingAt[request.from]; break;
      }
      const high = value > rule.threshold;
      if (high === (rule.mode === "min")) bits |= place;
      id = high ? rule.high : rule.low;
    }
    return bits;
  }

  function dispatch(time) {
    while (queue.length) {
      let bestCar, bestIndex, bestRequest = Infinity, bestDistance = Infinity, bestPriority = Infinity;
      for (const car of vehicles) {
        if (!car.idle) continue;
        for (const [i,id] of queue.entries()) {
          const request = arrivals[id];
          const de = model.distances[car.position][request.from];
          const score = priority(request,time,de);
          if (score < bestPriority || (score === bestPriority &&
              (de < bestDistance || (de === bestDistance && id < bestRequest)))) {
            bestCar = car;
            bestIndex = i;
            bestRequest = id;
            bestPriority = score;
            bestDistance = de;
          }
        }
      }
      if (!bestCar) return;
      const request = arrivals[bestRequest];
      queue.splice(bestIndex,1);
      waitingAt[request.from]--;
      bestCar.idle = false;
      bestCar.requestId = bestRequest;
      if (onDispatch) onDispatch({time,requestId:bestRequest,vehicleId:bestCar.id});
      travel(time,bestCar,request.from,false);
    }
  }

  while (events.peek()?.time <= model.T) {
    const time = events.peek().time;
    let needsDispatch = false;
    while (events.peek() && Math.abs(events.peek().time-time) < 1e-9) {
      const {kind,id} = events.pop();
      if (kind === REQUEST) {
        queue.push(id);
        waitingAt[arrivals[id].from]++;
        maxQueue = Math.max(maxQueue,queue.length);
        if (maxQueue > model.qcap) return {rejected:true,maxQueue,endQueue:queue.length};
        needsDispatch = true;
        continue;
      }
      const car = vehicles[id];
      if (kind === REACH) {
        const position = car.route[car.hop][0];
        car.position = position;
        let leave = time;
        if (model.intersections[position]) {
          leave = Math.max(time,freeAt[position]) + model.crossing;
          freeAt[position] = leave;
        }
        car.hop++;
        if (car.hop === car.route.length) stop(leave,car);
        else events.add(leave + car.route[car.hop][1],REACH,id);
      } else if (kind === LOADED) {
        travel(time,car,arrivals[car.requestId].to,true);
      } else {
        car.idle = true;
        needsDispatch = true;
      }
    }
    if (needsDispatch) dispatch(time);
  }

  const endQueue = queue.length;
  if (endQueue > model.endQmax) return {rejected:true,maxQueue,endQueue};
  let sum = 0, count = 0;
  const urgentWaits = [];
  for (const [id, request] of arrivals.entries()) {
    if (request.at < model.warm || pickup[id] < 0) continue;
    const wait = pickup[id]-request.at;
    count++;
    sum += wait;
    if (request.urgent) urgentWaits.push(wait);
  }
  if (!count || !urgentWaits.length) return {rejected:true,maxQueue,endQueue};
  urgentWaits.sort((a,b) => a-b);
  const averageWait = sum/count;
  const pos = 0.95*(urgentWaits.length-1);
  const low = Math.floor(pos), high = Math.ceil(pos);
  const urgent95 = urgentWaits[low] + (pos-low)*(urgentWaits[high]-urgentWaits[low]);
  const rate = model.flows.reduce((total,row) => total+row[2],0)*model.alpha;
  const cost = model.cAGV*policy.fleet + model.cW*rate*averageWait + model.cU*urgent95;
  return {rejected:false,cost,maxQueue,endQueue,averageWait,urgent95};
}
